"""Fixed-target, read-only VCTIP identity observation; never launches the target."""
import json
import sys

PROOF = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-fixture-detached-20261114/dev/windows/proof'
TARGET = (r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC'
          r'\14.44.35207\bin\Hostx64\x64\vctip.exe')
EXPECTED_SHA256 = 'd591358c6aee4b673562ca3b3a15c250e612708394fe3b44a7a7eda2470570a5'
EXPECTED_SIZE = 335288
VERSION_METADATA = '14.44.35229.0'


def main():
    if sys.platform != 'win32' or len(sys.argv) != 1:
        raise SystemExit('Windows and zero arguments are required')
    sys.path.insert(0, PROOF)
    from build_only.identity import FilePin
    from build_only.winapi import WinAPI

    api = WinAPI()
    with FilePin(api, TARGET, EXPECTED_SHA256, EXPECTED_SIZE) as pin:
        identity = {'path': TARGET, 'final_path': pin.final_path, 'sha256': pin.sha256.lower(),
                    'size': pin.size, 'volume_serial': pin.file_id[0], 'file_index_high': pin.file_id[1],
                    'file_index_low': pin.file_id[2], 'version_metadata': VERSION_METADATA,
                    'version_basis': 'previously observed metadata bound to pinned bytes; not re-observed'}
        pin.assert_path_unchanged()
    if api.close_failed or api.opened != api.closed:
        raise RuntimeError('owned identity handles did not close')
    identity['owned'] = {'opened': api.opened, 'closed': api.closed, 'failed': api.close_failed}
    print(json.dumps(identity, separators=(',', ':'), sort_keys=True), flush=True)


if __name__ == '__main__':
    main()
