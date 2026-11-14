"""Frozen, command-specific v2 profile; endpoint isolation is not yet qualified."""
from dataclasses import asdict, dataclass
import hashlib
import json
import ntpath
import re

from build_only.policy import CommandIdentity, Policy, environment_digest

TOOL = ('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/'
        '14.44.35207/bin/Hostx64/x64/')
ROOT = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-build-policy-20261113/deps/enrollment-cl-claim-20261114-01'
BASE = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-build-policy-20261113/dev/windows/proof/proposals/cl-cleanup-claim-v1'
ENDPOINT_NAME = '_MSPDBSRV_ENDPOINT_'
ENV_NAMES = ('_CL_', '_LINK_', '_MSPDBSRV_ENDPOINT_', 'APPDATA', 'CL', 'HEALTHY_TEST_SOURCE',
             'INCLUDE', 'JELLYFIN_TEST_OWNED_ROOT', 'LIB', 'LIBPATH', 'LINK', 'LOCALAPPDATA',
             'MPVQT_TEST_SOURCE', 'PATH', 'QT_QPA_PLATFORM', 'SystemRoot', 'TEMP', 'TMP')


@dataclass(frozen=True)
class RolePin:
    role: str
    path: str
    sha256: str
    size: int
    version: str
    file_id: str


VCTIP = RolePin('vctip', TOOL + 'vctip.exe',
                'd591358c6aee4b673562ca3b3a15c250e612708394fe3b44a7a7eda2470570a5',
                335288, '14.44.35229.0', '2555941978:196608:1477911')
MSPDBSRV = RolePin('mspdbsrv', TOOL + 'mspdbsrv.exe',
                   '8ffbacfdbef90a79e8441d640bc8b4c86be02a2d9035ce87c5fa4f36dc3c70e1',
                   186248, '14.44.35229.0', '2555941978:458752:1477178')


@dataclass(frozen=True)
class BuildProfile:
    schema: str
    identity: str
    command_policy: Policy
    residual_roles: tuple[RolePin, ...]
    endpoint: str
    env_names: tuple[str, ...]
    env_digest: str


def profile_hash(profile: BuildProfile) -> str:
    encoded = json.dumps(asdict(profile), sort_keys=True, ensure_ascii=True,
                         separators=(',', ':')).encode('ascii')
    return hashlib.sha256(encoded).hexdigest()


def _sha(value):
    return isinstance(value, str) and re.fullmatch(r'[0-9a-f]{64}', value) is not None


def _expected_command(endpoint, digest):
    response = BASE.replace('/', '\\') + r'\cleanup-claim.rsp'
    return CommandIdentity(
        executable=TOOL + 'cl.exe', executable_sha256='fe251ef50a1545b1b0835ee17b1e785459712b38d79e45b5c1d3d28970a36619',
        executable_size=677776, args=('@' + response,), cwd=ROOT,
        stdout=ROOT + '/compiler.stdout', stderr=ROOT + '/compiler.stderr', response_path=response,
        response_sha256='15d4a13278b38de63943cc4fc8776cbec79d44e4759a968d927ceb65f77de347',
        outputs=(ROOT + '/cleanup_claim.obj', ROOT + '/cleanup_claim-compile.pdb'),
        env_digest=digest, env_names=ENV_NAMES)


def validate_profile(profile: BuildProfile) -> BuildProfile:
    if not isinstance(profile, BuildProfile) or profile.schema != 'build-only-v2':
        raise ValueError('invalid profile schema')
    if not isinstance(profile.endpoint, str) or not re.fullmatch(r'jd-[0-9a-f]{32}', profile.endpoint):
        raise ValueError('invalid invocation endpoint')
    if profile.identity != 'cleanup-claim-cl-v2:' + profile.endpoint:
        raise ValueError('profile identity does not bind endpoint')
    if (not isinstance(profile.residual_roles, tuple) or
            any(not isinstance(item, RolePin) or not isinstance(item.version, str) or not item.version
                for item in profile.residual_roles)):
        raise ValueError('every production role requires an expected version')
    if profile.residual_roles != (VCTIP, MSPDBSRV):
        raise ValueError('profile must have exactly the two pinned roles')
    roles = profile.residual_roles
    if any(len({getattr(item, field) for item in roles}) != 2 for field in ('role', 'path', 'sha256', 'file_id')):
        raise ValueError('ambiguous residual identities')
    policy = profile.command_policy
    if not isinstance(policy, Policy) or not _sha(policy.source_sha256) or not _sha(profile.env_digest):
        raise ValueError('invalid source or environment digest')
    if (profile.env_names != ENV_NAMES or policy.command != _expected_command(profile.endpoint, profile.env_digest)
            or policy.identity != profile.identity or policy.kind != 'production' or policy.command_form != 'cl_response'
            or policy.deadline_ms != 60000 or policy.grace_ms != 1000):
        raise ValueError('command or timing differs from reviewed enrollment')
    if (policy.telemetry_path, policy.telemetry_sha256, policy.telemetry_size,
            policy.telemetry_version, policy.telemetry_file_id) != (
            VCTIP.path, VCTIP.sha256, VCTIP.size, VCTIP.version, VCTIP.file_id):
        raise ValueError('legacy telemetry pin differs')
    return profile


def validate_environment(profile: BuildProfile, environment) -> None:
    validate_profile(profile)
    folded = {name.casefold(): value for name, value in environment.items()}
    if len(folded) != len(environment) or not set(folded).issubset({name.casefold() for name in ENV_NAMES}):
        raise ValueError('environment contains duplicate or unreviewed names')
    if folded.get(ENDPOINT_NAME.casefold()) != profile.endpoint:
        raise ValueError('endpoint differs from profile')
    if any(folded.get(name.casefold(), '') for name in ('CL', '_CL_', 'LINK', '_LINK_')):
        raise ValueError('compiler option injection')
    if not folded.get('systemroot') or not ntpath.isabs(folded['systemroot']):
        raise ValueError('system root missing or relative')
    for name in ('path', 'include', 'lib', 'libpath'):
        value = folded.get(name)
        if not value or any(not part or not ntpath.isabs(part) for part in value.split(';')):
            raise ValueError('search path missing or relative')
    if environment_digest(ENV_NAMES, environment) != profile.env_digest:
        raise ValueError('environment digest mismatch')


def replay_environment(captured, endpoint):
    """Replay a separately hash-pinned private snapshot with the fixed overlay."""
    names = tuple(name for name in ENV_NAMES if name != ENDPOINT_NAME)
    if not isinstance(captured, dict) or set(captured) != set(names):
        raise ValueError('captured environment names differ')
    result = {}
    for name in names:
        entry = captured[name]
        if not isinstance(entry, dict) or set(entry) != {'present', 'value'} or type(entry['present']) is not bool:
            raise ValueError('invalid captured environment entry')
        if entry['present'] and isinstance(entry['value'], str):
            result[name] = entry['value']
        elif entry['present'] or entry['value'] is not None:
            raise ValueError('invalid captured environment value')
    for name, suffix in (('TEMP', 'temp'), ('TMP', 'temp'), ('APPDATA', 'roaming'), ('LOCALAPPDATA', 'local')):
        result[name] = ntpath.join(ROOT.replace('/', '\\'), suffix)
    result[ENDPOINT_NAME] = endpoint
    return result


def admit_production(_profile: BuildProfile) -> None:
    raise RuntimeError('production disabled pending pinned-CL endpoint isolation qualification')
