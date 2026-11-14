"""One fixed discovery compile; source proposal, not an execution grant."""
import json
import ntpath
import os
import sys

PROOF = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-fixture-detached-20261114/dev/windows/proof'
ROOT = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-build-policy-20261113/deps/enrollment-cl-claim-20261114-01'
BASE = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-build-policy-20261113/dev/windows/proof/proposals/cl-cleanup-claim-v1'
PRIVATE = 'E:/CursorAI/JellyfinDesktop/deps/reconcile-20261113/build-metadata-01/env-private/environment-private.json'
TOOL = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/'
NAMES = ('_CL_', '_LINK_', 'APPDATA', 'CL', 'HEALTHY_TEST_SOURCE', 'INCLUDE',
         'JELLYFIN_TEST_OWNED_ROOT', 'LIB', 'LIBPATH', 'LINK', 'LOCALAPPDATA',
         'MPVQT_TEST_SOURCE', 'PATH', 'QT_QPA_PLATFORM', 'SystemRoot', 'TEMP', 'TMP')
ENV_HASH = '1daa8974e1d37694f1067c92ab9839bab22d2f8a1d3fa844e0b2b58ea0c4512a'
FINAL_ENV = 'd9b37c10d4dd5d21ec86562737d8aea7dce17216e56733ae819b5dcfc4f3af01'


def _unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate environment field')
        result[key] = value
    return result


def _replay(FilePin, api, environment_digest):
    with FilePin(api, PRIVATE, ENV_HASH) as pin:
        if pin.size > 1048576:
            raise ValueError('private environment exceeds bound')
        with open(PRIVATE, 'rb') as source:
            captured = json.load(source, object_pairs_hook=_unique)
        pin.assert_path_unchanged()
    if set(captured) != set(NAMES):
        raise ValueError('private environment names differ')
    selected = {}
    for name in NAMES:
        entry = captured[name]
        if set(entry) != {'present', 'value'} or type(entry['present']) is not bool:
            raise ValueError('invalid environment entry')
        if entry['present'] and isinstance(entry['value'], str):
            selected[name] = entry['value']
        elif entry['present'] or entry['value'] is not None:
            raise ValueError('invalid environment value')
    if any(selected.get(name, '') for name in ('CL', '_CL_', 'LINK', '_LINK_')):
        raise ValueError('compiler or linker option injection')
    for name, suffix in (('TEMP', 'temp'), ('TMP', 'temp'), ('APPDATA', 'roaming'), ('LOCALAPPDATA', 'local')):
        selected[name] = ntpath.join(ROOT.replace('/', '\\'), suffix)
    if environment_digest(NAMES, selected) != FINAL_ENV:
        raise ValueError('environment identity mismatch')
    os.environ.clear()
    os.environ.update(selected)
    if environment_digest(NAMES, os.environ) != FINAL_ENV:
        raise ValueError('environment replay mismatch')


def main():
    if sys.platform != 'win32' or len(sys.argv) != 1:
        raise SystemExit('Windows and zero arguments required')
    sys.path.insert(0, PROOF)
    from build_only.identity import FilePin
    from build_only.policy import CommandIdentity, Policy, PRODUCTION_POLICIES, environment_digest
    from build_only.supervisor import production
    from build_only.winapi import WinAPI
    api = WinAPI()
    if not api.in_any_job():
        raise RuntimeError('outer helper job required')
    _replay(FilePin, api, environment_digest)
    if api.opened != api.closed or api.close_failed:
        raise RuntimeError('environment pin did not close')
    response = BASE.replace('/', '\\') + r'\cleanup-claim.rsp'
    command = CommandIdentity(
        executable=TOOL + 'cl.exe', executable_sha256='fe251ef50a1545b1b0835ee17b1e785459712b38d79e45b5c1d3d28970a36619',
        executable_size=677776, args=('@' + response,), cwd=ROOT,
        stdout=ROOT + '/compiler.stdout', stderr=ROOT + '/compiler.stderr', response_path=response,
        response_sha256='15d4a13278b38de63943cc4fc8776cbec79d44e4759a968d927ceb65f77de347',
        outputs=(ROOT + '/cleanup_claim.obj', ROOT + '/cleanup_claim-compile.pdb'),
        env_digest=FINAL_ENV, env_names=NAMES)
    name = 'cleanup-claim-cl-20261114'
    policy = Policy(identity=name, kind='production', command_form='cl_response',
        source_sha256='80b842ae0d7ad4c3384b9eacfb9c492962febebae449c90a2b18d6b1e4f25915',
        telemetry_path=TOOL + 'vctip.exe', telemetry_sha256='d591358c6aee4b673562ca3b3a15c250e612708394fe3b44a7a7eda2470570a5',
        telemetry_size=335288, telemetry_version='14.44.35229.0',
        telemetry_file_id='2555941978:196608:1477911', command=command, deadline_ms=60000, grace_ms=1000)
    if PRODUCTION_POLICIES:
        raise RuntimeError('production registry must start empty')
    PRODUCTION_POLICIES[name] = policy
    production(name, ROOT + '/build.receipt')


if __name__ == '__main__':
    main()
