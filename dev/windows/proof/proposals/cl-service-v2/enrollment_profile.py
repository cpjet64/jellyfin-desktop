"""Construct metadata for the fixed CL profile; this module launches nothing."""
from build_only.policy import Policy, environment_digest
from build_only_v2.profile import (BuildProfile, ENV_NAMES, MSPDBSRV, VCTIP,
                                   _expected_command, replay_environment,
                                   validate_environment, validate_profile)


def build_profile(endpoint, captured, package_sha256):
    """Bind an already-selected fresh endpoint and hash-pinned private snapshot.

    This does not allocate an endpoint, prove isolation, register a policy, or
    permit production execution. The source digest must come from the reviewed
    assembled candidate; no digest is fabricated here.
    """
    environment = replay_environment(captured, endpoint)
    digest = environment_digest(ENV_NAMES, environment)
    identity = 'cleanup-claim-cl-v2:' + endpoint
    command = _expected_command(endpoint, digest)
    policy = Policy(identity=identity, kind='production', command_form='cl_response',
                    source_sha256=package_sha256, telemetry_path=VCTIP.path,
                    telemetry_sha256=VCTIP.sha256, telemetry_size=VCTIP.size,
                    telemetry_version=VCTIP.version, telemetry_file_id=VCTIP.file_id,
                    command=command, deadline_ms=60000, grace_ms=1000)
    profile = BuildProfile(schema='build-only-v2', identity=identity, command_policy=policy,
                           residual_roles=(VCTIP, MSPDBSRV), endpoint=endpoint,
                           env_names=ENV_NAMES, env_digest=digest)
    validate_profile(profile)
    validate_environment(profile, environment)
    return profile, environment
