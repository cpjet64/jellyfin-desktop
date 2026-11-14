"""Build-only command policy qualification cases."""
import unittest
from build_only.policy import CommandIdentity, Policy, environment_digest, validate_command


def fixture_policy():
    environment = {"CL": ""}
    command = CommandIdentity(r"C:\tools\cl.exe", "a" * 64, 1, ("@C:\\work\\a.rsp",),
                              r"C:\work", r"C:\work\out.log", r"C:\work\err.log",
                              r"C:\work\a.rsp", "b" * 64, (r"C:\work\a.obj",),
                              environment_digest(("CL",), environment), ("CL",))
    return Policy("fixture", "fixture", "fixture", "c" * 64, r"C:\tools\vctip.exe",
                  "d" * 64, 1, "1", "file-id", command, 1000, 100)


class BuildOnlyPolicyTests(unittest.TestCase):
    def test_fixture_manifest(self):
        policy = fixture_policy()
        self.assertEqual(validate_command(policy, policy.command), policy.command)

    def test_missing_environment_differs_from_injection(self):
        self.assertNotEqual(environment_digest(("CL",), {}),
                            environment_digest(("CL",), {"CL": "/DATTACK"}))

    def test_missing_and_empty_are_distinct(self):
        self.assertNotEqual(environment_digest(("CL",), {}),
                            environment_digest(("CL",), {"CL": ""}))
