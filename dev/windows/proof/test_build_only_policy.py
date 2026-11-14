"""Build-only command policy qualification cases."""
import unittest
from dataclasses import replace
from unittest.mock import patch
from build_only.policy import (CommandIdentity, Policy, PRODUCTION_POLICIES, VCTIP_PATH, VCTIP_SHA256,
                               environment_digest, inspect_command_inputs, validate_command)


def fixture_policy():
    environment = {"CL": ""}
    command = CommandIdentity(r"C:\tools\cl.exe", "a" * 64, 1, ("@C:\\work\\a.rsp",),
                              r"C:\work", r"C:\work\out.log", r"C:\work\err.log",
                              r"C:\work\a.rsp", "b" * 64, (r"C:\work\a.obj",),
                              environment_digest(("CL",), environment), ("CL",))
    return Policy("fixture", "fixture", "fixture", "c" * 64, r"C:\tools\vctip.exe",
                  "d" * 64, 1, "1", "file-id", command, 1000, 100)


class BuildOnlyPolicyTests(unittest.TestCase):
    def test_native_diagnostic_selector(self):
        from test_build_only_native import _selected_cases
        self.assertEqual(_selected_cases(None), ("natural", "residual", "rootfail"))
        self.assertEqual(_selected_cases("residual"), ("residual",))
        with self.assertRaises(ValueError):
            _selected_cases("other")

    def test_fixture_manifest(self):
        policy = fixture_policy()
        self.assertEqual(validate_command(policy, policy.command), policy.command)

    def test_missing_environment_differs_from_injection(self):
        self.assertNotEqual(environment_digest(("CL",), {}),
                            environment_digest(("CL",), {"CL": "/DATTACK"}))

    def test_missing_and_empty_are_distinct(self):
        self.assertNotEqual(environment_digest(("CL",), {}),
                            environment_digest(("CL",), {"CL": ""}))

    def test_changed_manifest_identity_rejected(self):
        policy = fixture_policy()
        for changes in ({"args": ("other",)}, {"executable_sha256": "f" * 64},
                        {"response_sha256": "f" * 64}, {"outputs": (r"C:\work\other.obj",)}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                validate_command(policy, replace(policy.command, **changes))

    def test_production_environment_coverage_required(self):
        fixture = fixture_policy()
        policy = replace(fixture, kind="production", command_form="cl_response",
                         telemetry_path=VCTIP_PATH, telemetry_sha256=VCTIP_SHA256,
                         telemetry_size=335288, telemetry_version="14.44.35229.0")
        with patch.dict(PRODUCTION_POLICIES, {policy.identity: policy}):
            with self.assertRaises(ValueError):
                validate_command(policy, policy.command)

    def test_changed_files_and_stale_output_rejected(self):
        policy = fixture_policy()
        environment = {"CL": ""}
        good = [(policy.command.executable_sha256, policy.command.executable_size),
                (policy.command.response_sha256, 1)]
        for hashes, stale in (([("f" * 64, 1)], False),
                              ([good[0], ("f" * 64, 1)], False), (good, True)):
            with self.subTest(hashes=hashes, stale=stale):
                with patch("build_only.policy._file_hash", side_effect=hashes), \
                     patch("build_only.policy.os.path.lexists", return_value=stale):
                    with self.assertRaises(ValueError):
                        inspect_command_inputs(policy, environment)
