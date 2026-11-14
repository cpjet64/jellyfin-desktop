"""Build-only receipt evidence qualification cases."""
from dataclasses import asdict
import json
import unittest
from build_only.policy import policy_hash
from build_only.receipt import validate_receipt
from test_build_only_policy import fixture_policy


def make_receipt(cleanup=False):
    policy = fixture_policy()
    command = asdict(policy.command)
    command["args"] = list(command["args"])
    command["outputs"] = list(command["outputs"])
    command["env_names"] = list(command["env_names"])
    pin = {"path": policy.telemetry_path, "sha256": policy.telemetry_sha256,
           "size": policy.telemetry_size, "version": policy.telemetry_version,
           "file_id": policy.telemetry_file_id}
    residual = {"pid": 2, **pin, "member": True, "action": "already_exited",
                "signaled": True, "closed": True}
    active = int(cleanup)
    record = {"schema": "build-only-v1", "source": policy.source_sha256,
              "policy": policy_hash(policy), "policy_kind": "fixture",
              "result": "telemetry_cleanup" if cleanup else "natural", "command": command,
              "root": {"pid": 1, "creation_time": 1, "exit": 0, "signaled": True,
                       "observed_ms": 200, "closed": True},
              "deadlines": {"start_ms": 100, "root_deadline_ms": 1100,
                            "grace_deadline_ms": 300, "cleanup_start_ms": 300,
                            "cleanup_deadline_ms": 10300, "settled_ms": 400},
              "file_pin": pin, "residuals": [residual] if cleanup else [],
              "baseline": {"total": 2, "active": active},
              "before": {"total": 2, "active": 0}, "final": {"total": 2, "active": 0},
              "owned": {"opened": 3, "closed": 3, "failed": 0},
              "forced_cleanup": cleanup, "emergency_cleanup": False,
              "outputs": [{"path": policy.command.outputs[0], "sha256": "e" * 64,
                           "size": 1, "fresh_before_launch": True}],
              "finalization": {"exclusive": True, "write_complete": True}, "errors": []}
    return policy, record


def check(policy, record, **options):
    return validate_receipt(json.dumps(record).encode(), expected_source=policy.source_sha256,
                            expected_policy=policy, observed_exit=options.get("exit", 0),
                            observed_receipt_closed=options.get("closed", True),
                            production=options.get("production", False))


class ReceiptTests(unittest.TestCase):
    def test_natural_and_cleanup(self):
        for cleanup in (False, True):
            policy, record = make_receipt(cleanup)
            self.assertEqual(check(policy, record)["result"], record["result"])

    def test_fixture_rejected_for_production(self):
        policy, record = make_receipt()
        with self.assertRaises(ValueError):
            check(policy, record, production=True)

    def test_failure_cases(self):
        cases = [("root", "exit", 1), ("root", "observed_ms", 1101),
                 ("root", "pid", True), ("before", "total", 3),
                 ("owned", "failed", 1), ("outputs", "fresh_before_launch", False),
                 ("file_pin", "file_id", "other")]
        for section, key, value in cases:
            policy, record = make_receipt()
            target = record[section][0] if section == "outputs" else record[section]
            target[key] = value
            with self.subTest(section=section, key=key), self.assertRaises(ValueError):
                check(policy, record)

    def test_emergency_and_bad_residual(self):
        for change in ("emergency", "wait", "identity"):
            policy, record = make_receipt(True)
            if change == "emergency":
                record["emergency_cleanup"] = True
            elif change == "wait":
                record["residuals"][0]["signaled"] = False
            else:
                record["residuals"][0]["sha256"] = "f" * 64
            with self.subTest(change=change), self.assertRaises(ValueError):
                check(policy, record)

    def test_closure_and_malformed(self):
        policy, record = make_receipt()
        for options in ({"exit": 1}, {"closed": False}):
            with self.assertRaises(ValueError):
                check(policy, record, **options)
        del record["root"]["closed"]
        with self.assertRaises(ValueError):
            check(policy, record)

    def test_duplicate_json_field(self):
        policy, record = make_receipt()
        raw = json.dumps(record).replace('"schema":', '"schema": 1, "schema":', 1).encode()
        with self.assertRaises(ValueError):
            validate_receipt(raw, expected_source=policy.source_sha256, expected_policy=policy,
                             observed_exit=0, observed_receipt_closed=True, production=False)
