"""Fixed, read-only metadata proposal. Execution needs a separate allocation."""
import hashlib
import json
import ntpath
import os
import sys

PROOF = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-fixture-detached-20261114/dev/windows/proof'
POLICY_ROOT = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-build-policy-20261113'
PROPOSAL = POLICY_ROOT + '/dev/windows/proof/proposals/cl-cleanup-claim-v1/proposal.json'
RESPONSE = POLICY_ROOT + '/dev/windows/proof/proposals/cl-cleanup-claim-v1/cleanup-claim.rsp'
PRIVATE_ENV = 'E:/CursorAI/JellyfinDesktop/deps/reconcile-20261113/build-metadata-01/env-private/environment-private.json'
OUTPUT_ROOT = POLICY_ROOT + '/deps/enrollment-cl-claim-20261114-01'
TOOLS = ('c1xx.dll', 'mspdb140.dll', 'mspdbcore.dll', 'mspdbsrv.exe', 'mspdbst.dll', 'mspft140.dll')
TOOL_DIR = ('C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/'
            '14.44.35207/bin/Hostx64/x64')
QT_CORE = 'E:/CursorAI/JellyfinDesktop/dev/windows/deps/qt/6.9.3/msvc2022_64/include/QtCore/'
QT_WRAPPERS = (('QCoreApplication', 'b81a7b673a789290254c8a0696f2062f1871086194e98809ab42c87ec9eed413', 54),
               ('QThread', '449ec25b9b7ee35a8be5ecdda3de7b84e8e0ad9bfe9e6321511d6f4734b37d04', 45))
NAMES = ('_CL_', '_LINK_', 'APPDATA', 'CL', 'HEALTHY_TEST_SOURCE', 'INCLUDE',
         'JELLYFIN_TEST_OWNED_ROOT', 'LIB', 'LIBPATH', 'LINK', 'LOCALAPPDATA',
         'MPVQT_TEST_SOURCE', 'PATH', 'QT_QPA_PLATFORM', 'SystemRoot', 'TEMP', 'TMP')
ENV_HASH = '1daa8974e1d37694f1067c92ab9839bab22d2f8a1d3fa844e0b2b58ea0c4512a'
PROPOSAL_HASH = '02d8d582436ffb18c621efa0556c5f3225a6fcf0cc13c9795a854a40f3488f07'


def _unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate metadata field')
        result[key] = value
    return result


def _read_locked(api, FilePin, path, digest, size=None):
    with FilePin(api, path, digest, size) as pin:
        if pin.size > 1048576:
            raise ValueError('metadata input exceeds bound')
        with open(path, 'rb') as stream:
            return stream.read()


def _inspect(api, FilePin, path, expected_hash, expected_size=None):
    with FilePin(api, path, expected_hash, expected_size) as pin:
        pin.assert_path_unchanged()
        return {'path': path, 'final_path': pin.final_path, 'sha256': pin.sha256.lower(),
                'size': pin.size, 'file_id': list(pin.file_id)}


def _observe_tool(api, FilePin, path):
    if os.path.getsize(path) > 134217728:
        raise ValueError('support tool exceeds bound')
    digest = hashlib.sha256()
    with open(path, 'rb') as stream:
        for block in iter(lambda: stream.read(65536), b''):
            digest.update(block)
    return _inspect(api, FilePin, path, digest.hexdigest())


def _environment(api, FilePin, environment_digest):
    raw = _read_locked(api, FilePin, PRIVATE_ENV, ENV_HASH)
    entries = json.loads(raw, object_pairs_hook=_unique)
    if set(entries) != set(NAMES):
        raise ValueError('private environment names differ')
    result = {}
    for name in NAMES:
        entry = entries[name]
        if set(entry) != {'present', 'value'} or type(entry['present']) is not bool:
            raise ValueError('invalid private environment entry')
        if entry['present']:
            if not isinstance(entry['value'], str):
                raise ValueError('invalid private environment value')
            result[name] = entry['value']
        elif entry['value'] is not None:
            raise ValueError('invalid absent private environment entry')
    for name in ('CL', '_CL_', 'LINK', '_LINK_'):
        if result.get(name, '') != '':
            raise ValueError('compiler or linker option injection present')
    for name, suffix in (('TEMP', 'temp'), ('TMP', 'temp'), ('APPDATA', 'roaming'), ('LOCALAPPDATA', 'local')):
        result[name] = ntpath.join(OUTPUT_ROOT.replace('/', '\\'), suffix)
    for name in ('PATH', 'INCLUDE', 'LIB', 'LIBPATH'):
        if not result.get(name) or any(not part or not ntpath.isabs(part) for part in result[name].split(';')):
            raise ValueError('search path is absent or contains a relative component')
    return environment_digest(NAMES, result)


def _response(api, FilePin):
    raw = _read_locked(api, FilePin, RESPONSE, '15d4a13278b38de63943cc4fc8776cbec79d44e4759a968d927ceb65f77de347', 3129)
    text = raw.decode('ascii')
    args = text.split()
    if len(args) != 67 or '"' in text or any(arg.startswith('@') for arg in args) or args.count('/showIncludes') != 1:
        raise ValueError('unexpected response argument shape')
    for arg in args:
        if arg.startswith('-I') and not ntpath.isabs(arg[2:]):
            raise ValueError('relative include path')
        if arg.startswith('-external:I') and not ntpath.isabs(arg[len('-external:I'):]):
            raise ValueError('relative external include path')
        if arg.startswith(('/Fo', '/Fd')) and not ntpath.isabs(arg[3:]):
            raise ValueError('relative compiler output path')
    if not ntpath.isabs(args[-1]):
        raise ValueError('relative source path')
    return {'path': RESPONSE, 'sha256': hashlib.sha256(raw).hexdigest(), 'bytes': len(raw),
            'argument_count': len(args), 'show_includes': True}


def main():
    if sys.platform != 'win32' or len(sys.argv) != 1:
        raise SystemExit('Windows and zero arguments are required')
    if os.path.lexists(OUTPUT_ROOT):
        raise ValueError('output root is not fresh')
    sys.path.insert(0, PROOF)
    from build_only.identity import FilePin
    from build_only.policy import environment_digest
    from build_only.winapi import WinAPI
    api = WinAPI()
    if not api.in_any_job():
        raise RuntimeError('qualified outer helper job is required')
    proposal = json.loads(_read_locked(api, FilePin, PROPOSAL, PROPOSAL_HASH), object_pairs_hook=_unique)
    inputs = []
    for entry in proposal['observed_inputs_not_a_complete_dependency_lock']:
        path = entry['path']
        if not ntpath.isabs(path):
            path = ntpath.join(proposal['candidate']['worktree'].replace('/', '\\'), path.replace('/', '\\'))
        inputs.append(_inspect(api, FilePin, path, entry['sha256'], entry['bytes']))
    for name, digest, size in QT_WRAPPERS:
        inputs.append(_inspect(api, FilePin, QT_CORE + name, digest, size))
    source = ntpath.join(proposal['candidate']['worktree'].replace('/', '\\'), proposal['candidate']['source'].replace('/', '\\'))
    inputs.append(_inspect(api, FilePin, source, proposal['candidate']['source_sha256'], proposal['candidate']['source_bytes']))
    compiler = proposal['compiler']
    telemetry = proposal['telemetry']
    report = {'schema': 'compile-preflight-v1', 'source_sha': proposal['candidate']['sha'],
              'compiler': _inspect(api, FilePin, compiler['path'], compiler['sha256'], compiler['bytes']),
              'telemetry': _inspect(api, FilePin, telemetry['path'], telemetry['sha256'], telemetry['bytes']),
              'direct_inputs': inputs, 'support_tools': [_observe_tool(api, FilePin, ntpath.join(TOOL_DIR, name)) for name in TOOLS],
              'response': _response(api, FilePin), 'private_environment_digest': _environment(api, FilePin, environment_digest),
              'injection_check': 'CL, _CL_, LINK and _LINK_ absent or empty',
              'cwd': OUTPUT_ROOT, 'output_root_fresh': True,
              'cwd_independence': 'absolute include and environment search paths checked; implicit compiler behavior unproven',
              'generated_include': {'path': 'E:/CursorAI/JellyfinDesktop/deps/worktrees/integration/deps/w1192-healthy-build/tests/test_healthy_cleanup_claim_autogen/include',
                                    'exists': os.path.isdir('E:/CursorAI/JellyfinDesktop/deps/worktrees/integration/deps/w1192-healthy-build/tests/test_healthy_cleanup_claim_autogen/include'),
                                    'qualification': 'historical directory; unqualified'},
              'dependencies': 'partial direct inputs only; transitive closure awaits discovery compile'}
    if api.close_failed or api.opened != api.closed:
        raise RuntimeError('preflight owned handles did not close')
    report['owned'] = {'opened': api.opened, 'closed': api.closed, 'failed': api.close_failed}
    print(json.dumps(report, separators=(',', ':'), sort_keys=True), flush=True)


if __name__ == '__main__':
    main()
