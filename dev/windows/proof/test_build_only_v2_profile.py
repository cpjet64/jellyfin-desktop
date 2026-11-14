"""Pure source tests for the disabled command-specific profile."""
from dataclasses import FrozenInstanceError, replace
import unittest

from build_only.policy import Policy, environment_digest
from build_only_v2.profile import (BuildProfile, ENV_NAMES, MSPDBSRV, VCTIP,
                                   _expected_command, admit_production, profile_hash,
                                   replay_environment, validate_environment, validate_profile)


def sample():
    endpoint = 'jd-' + '1' * 32
    env = {'_MSPDBSRV_ENDPOINT_': endpoint, 'PATH': r'C:\Tools', 'SystemRoot': r'C:\Windows',
           'INCLUDE': r'C:\Include', 'LIB': r'C:\Lib', 'LIBPATH': r'C:\Lib'}
    digest = environment_digest(ENV_NAMES, env)
    identity = 'cleanup-claim-cl-v2:' + endpoint
    policy = Policy(identity=identity, kind='production', command_form='cl_response',
                    source_sha256='1' * 64, telemetry_path=VCTIP.path, telemetry_sha256=VCTIP.sha256,
                    telemetry_size=VCTIP.size, telemetry_version=VCTIP.version,
                    telemetry_file_id=VCTIP.file_id, command=_expected_command(endpoint, digest),
                    deadline_ms=60000, grace_ms=1000)
    return BuildProfile('build-only-v2', identity, policy, (VCTIP, MSPDBSRV), endpoint, ENV_NAMES, digest), env


class ProfileTests(unittest.TestCase):
    def test_exact_profile_and_environment(self):
        profile, env = sample()
        self.assertIs(validate_profile(profile), profile)
        self.assertIsNone(validate_environment(profile, env))
        self.assertEqual(len(profile_hash(profile)), 64)
        self.assertIsNone(MSPDBSRV.version)

    def test_duplicate_or_unknown_roles_rejected(self):
        profile, _ = sample()
        for roles in ((VCTIP, VCTIP), (MSPDBSRV, VCTIP), (VCTIP,), (VCTIP, MSPDBSRV, VCTIP)):
            with self.subTest(roles=roles), self.assertRaises(ValueError):
                validate_profile(replace(profile, residual_roles=roles))

    def test_altered_pin_and_command_rejected(self):
        profile, _ = sample()
        altered = replace(MSPDBSRV, file_id=VCTIP.file_id)
        with self.assertRaises(ValueError):
            validate_profile(replace(profile, residual_roles=(VCTIP, altered)))
        policy = replace(profile.command_policy, grace_ms=1001)
        with self.assertRaises(ValueError):
            validate_profile(replace(profile, command_policy=policy))

    def test_endpoint_and_digest_are_bound(self):
        profile, env = sample()
        with self.assertRaises(ValueError):
            validate_environment(profile, dict(env, _MSPDBSRV_ENDPOINT_='jd-' + '2' * 32))
        with self.assertRaises(ValueError):
            validate_environment(profile, dict(env, CL='/unexpected'))
        with self.assertRaises(ValueError):
            validate_environment(profile, dict(env, UNREVIEWED='value'))

    def test_production_admission_is_unconditionally_closed(self):
        profile, _ = sample()
        with self.assertRaises(RuntimeError):
            admit_production(profile)
        with self.assertRaises(FrozenInstanceError):
            profile.endpoint = 'jd-' + '2' * 32

    def test_snapshot_replay_preserves_absent_and_empty(self):
        captured = {name: {'present': False, 'value': None}
                    for name in ENV_NAMES if name != '_MSPDBSRV_ENDPOINT_'}
        captured['CL'] = {'present': True, 'value': ''}
        result = replay_environment(captured, 'jd-' + '1' * 32)
        self.assertEqual(result['CL'], '')
        self.assertNotIn('_CL_', result)
        self.assertEqual(result['_MSPDBSRV_ENDPOINT_'], 'jd-' + '1' * 32)
        self.assertTrue(result['TEMP'].endswith(r'enrollment-cl-claim-20261114-01\temp'))
        del captured['PATH']
        with self.assertRaises(ValueError):
            replay_environment(captured, 'jd-' + '1' * 32)


if __name__ == '__main__':
    unittest.main()
