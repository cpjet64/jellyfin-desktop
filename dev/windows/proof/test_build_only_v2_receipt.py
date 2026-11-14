"""Source tests of strict v2 receipt and independent readback contracts."""
from dataclasses import asdict, dataclass
import json
import unittest
from unittest.mock import patch

from build_only_v2 import postcheck, receipt
from test_build_only_policy import fixture_policy


@dataclass(frozen=True)
class Pin:
    role: str
    path: str
    sha256: str
    size: int
    version: str | None
    file_id: str


@dataclass(frozen=True)
class Profile:
    command_policy: object
    residual_roles: tuple
    endpoint: str = "unique-endpoint"
    env_digest: str = "a" * 64


def example(roles=("vctip", "mspdbsrv")):
    policy = fixture_policy()
    pins = (Pin("vctip", "vctip", "b" * 64, 1, "1", "one"),
            Pin("mspdbsrv", "mspdbsrv", "c" * 64, 2, None, "two"))
    profile = Profile(policy, pins)
    command = asdict(policy.command)
    for name in ("args", "outputs", "env_names"):
        command[name] = list(command[name])
    members = []
    for index, role in enumerate(roles):
        pin = asdict(next(p for p in pins if p.role == role))
        pin.pop("role")
        members.append({"pid": index + 2, "role": role,
                        "identity": {"creation_time": index + 2, **pin}, "member": True,
                        "action": "terminate", "signaled": True, "closed": True})
    result = "natural" if not roles else ("build_service_cleanup" if "mspdbsrv" in roles else "telemetry_cleanup")
    record = {"schema": "build-only-v2", "source": policy.source_sha256, "profile": "profile-hash",
              "endpoint": profile.endpoint, "env_digest": profile.env_digest, "command": command,
              "root": {"pid": 1, "creation_time": 1, "exit": 0, "signaled": True,
                       "observed_ms": 200, "closed": True},
              "deadlines": {"start_ms": 100, "root_deadline_ms": 1100, "grace_deadline_ms": 300,
                            "cleanup_start_ms": 300, "cleanup_deadline_ms": 10300, "settled_ms": 400},
              "pins": [asdict(pin) for pin in pins], "baseline": {"total": 3, "active": len(roles)},
              "before": {"total": 3, "active": len(roles)}, "final": {"total": 3, "active": 0},
              "verified_all_ms": 310, "intervention_ms": 320, "residuals": members, "result": result,
              "owned": {"opened": 5, "closed": 5, "failed": 0}, "emergency_cleanup": False,
              "outputs": [{"path": policy.command.outputs[0], "sha256": "e" * 64, "size": 1,
                           "fresh_before_launch": True}], "finalization": {"exclusive": True,
                           "write_complete": True}, "errors": []}
    return profile, record


def check(profile, record, **kwargs):
    with patch.object(receipt, "validate_profile"), patch.object(receipt, "profile_hash", return_value="profile-hash"):
        return receipt.validate_receipt(json.dumps(record).encode(), expected_profile=profile,
            expected_source=profile.command_policy.source_sha256, observed_exit=kwargs.get("exit", 0),
            observed_receipt_closed=kwargs.get("closed", True), production=kwargs.get("production", False))


class ReceiptV2Tests(unittest.TestCase):
    def test_role_classification(self):
        for roles in ((), ("vctip",), ("mspdbsrv",), ("vctip", "mspdbsrv")):
            profile, record = example(roles)
            self.assertEqual(check(profile, record)["result"], record["result"])

    def test_rejects_failed_evidence(self):
        for section, key, value in (("root", "exit", 1), ("before", "total", 4),
                                    ("owned", "failed", 1), ("deadlines", "settled_ms", 10300),
                                    ("residuals", "closed", False), ("residuals", "signaled", False)):
            profile, record = example()
            target = record[section][0] if section == "residuals" else record[section]
            target[key] = value
            with self.subTest(section=section, key=key), self.assertRaises(ValueError):
                check(profile, record)

    def test_rejects_identity_order_and_classification(self):
        for case in ("unknown", "duplicate", "profile", "early", "wrong_result", "root"):
            profile, record = example()
            if case == "unknown": record["residuals"][0]["identity"]["file_id"] = "unknown"
            if case == "duplicate": record["residuals"][1] = record["residuals"][0].copy()
            if case == "profile": record["pins"][1]["sha256"] = "d" * 64
            if case == "early": record["intervention_ms"] = 309
            if case == "wrong_result": record["result"] = "telemetry_cleanup"
            if case == "root": record["residuals"][0]["pid"] = 1; record["residuals"][0]["identity"]["creation_time"] = 1
            with self.subTest(case=case), self.assertRaises(ValueError):
                check(profile, record)

    def test_production_and_outer_fail_closed(self):
        profile, record = example()
        for kwargs in ({"production": True}, {"exit": 1}, {"closed": False}):
            with self.assertRaises(ValueError): check(profile, record, **kwargs)

    def test_v1_consumer_and_duplicate_json_reject_v2(self):
        from build_only.receipt import validate_receipt as old_validate
        profile, record = example()
        raw = json.dumps(record).encode()
        with self.assertRaises(ValueError):
            old_validate(raw, expected_source=profile.command_policy.source_sha256,
                         expected_policy=profile.command_policy, observed_exit=0,
                         observed_receipt_closed=True, production=False)
        raw = raw[:-1] + b',"schema":"build-only-v2"}'
        with patch.object(receipt, "validate_profile"), patch.object(receipt, "profile_hash", return_value="profile-hash"):
            with self.assertRaises(ValueError):
                receipt.validate_receipt(raw, expected_profile=profile,
                    expected_source=profile.command_policy.source_sha256, observed_exit=0,
                    observed_receipt_closed=True, production=False)

    def test_outer_receipt_rejects_failure(self):
        raw = (b"receipt_version=1\nroot_pid=2\nroot_exit=0\ntimeout=0 active=0\n"
               b"handles_before=1 handles_after=1 measured=1\nowned_opened=6 owned_closed=6 close_failed=0\n"
               b"handles_complete=1\nfinal_result=0\nproof_complete=1\n")
        self.assertEqual(postcheck._outer_exit(raw), 0)
        with self.assertRaises(ValueError): postcheck._outer_exit(raw.replace(b"root_exit=0", b"root_exit=1"))

    def test_readback_binds_actual_output_and_closes_receipt(self):
        profile, record = example()
        path = profile.command_policy.command.outputs[0]

        class Api:
            def __init__(self): self.opened = self.closed = self.close_failed = 0

        class Handle:
            def __init__(self, api, name):
                self.api, self.name = api, name
                api.opened += 1
            def close(self): self.api.closed += 1

        def read(api, handle, maximum, retain):
            if handle.name == r"C:\work\outer": return b"outer", 5, "x"
            if handle.name == r"C:\work\receipt": return b"receipt", 7, "y"
            return None, 1, "e" * 64

        def validate(*args, **kwargs):
            self.assertTrue(kwargs["observed_receipt_closed"])
            return record

        with patch.object(postcheck.w, "WinAPI", Api), patch.object(postcheck, "_open", side_effect=Handle), \
             patch.object(postcheck, "_read", side_effect=read), patch.object(postcheck, "_outer_exit", return_value=0), \
             patch.object(postcheck, "validate_receipt", side_effect=validate):
            result = postcheck.postcheck(receipt_path=r"C:\work\receipt", outer_path=r"C:\work\outer", expected_profile=profile,
                expected_source=profile.command_policy.source_sha256, observed_helper_exit=0, production=False)
            self.assertEqual(result["outputs"][0]["path"], path)
            record["outputs"][0]["sha256"] = "f" * 64
            with self.assertRaises(ValueError):
                postcheck.postcheck(receipt_path=r"C:\work\receipt", outer_path=r"C:\work\outer", expected_profile=profile,
                    expected_source=profile.command_policy.source_sha256, observed_helper_exit=0, production=False)
