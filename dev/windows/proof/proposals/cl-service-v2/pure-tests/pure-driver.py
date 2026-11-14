"""Fixed pure suites only; run under the separately reviewed retained-input controller."""
import importlib.util
import ctypes
import os
import subprocess
import sys
import unittest
import _winapi

V1 = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-fixture-detached-20261114/dev/windows/proof'
V2 = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/compiler-contract-v2-review-20261114/dev/windows/proof'
ROOT = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/compiler-contract-v2-review-20261114/deps/pure-test-v2-20261114-02'
SOURCES = (('v2_profile_tests', V2 + '/test_build_only_v2_profile.py'),
           ('v2_receipt_tests', V2 + '/test_build_only_v2_receipt.py'),
           ('v2_lifecycle_tests', V2 + '/tests/test_v2_lifecycle_contract.py'))


def forbidden(*_args, **_kwargs):
    raise RuntimeError('native execution forbidden in pure source suites')


def main():
    cache = ROOT + '/pycache'
    if (sys.platform != 'win32' or not sys.flags.isolated or not sys.flags.no_site
            or not sys.dont_write_bytecode or not sys.pycache_prefix
            or os.path.normcase(os.path.abspath(sys.pycache_prefix)) != os.path.normcase(os.path.abspath(cache))
            or not os.path.isdir(cache) or os.listdir(cache)):
        raise RuntimeError('isolated fresh-cache invocation required')
    sys.path[:0] = [V1, V2]
    from build_only import winapi
    winapi.WinAPI = forbidden
    ctypes.WinDLL = ctypes.CDLL = ctypes.PyDLL = forbidden
    _winapi.CreateProcess = forbidden
    subprocess.Popen = subprocess.run = subprocess.call = subprocess.check_call = subprocess.check_output = forbidden
    os.system = os.popen = os.spawnl = os.spawnle = os.spawnlp = os.spawnlpe = forbidden
    os.spawnv = os.spawnve = os.spawnvp = os.spawnvpe = forbidden
    os.startfile = forbidden
    os.execv = os.execve = os.execl = os.execle = os.execlp = os.execlpe = forbidden
    os.execvp = os.execvpe = forbidden
    suites = unittest.TestSuite()
    for name, path in SOURCES:
        spec = importlib.util.spec_from_file_location(name, path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        suites.addTests(unittest.defaultTestLoader.loadTestsFromModule(module))
    for name, module in tuple(sys.modules.items()):
        if name == 'build_only' or name.startswith('build_only.'):
            origin = getattr(module, '__file__', '')
            if not os.path.normcase(os.path.abspath(origin)).startswith(os.path.normcase(os.path.abspath(V1)) + os.sep):
                raise RuntimeError('v1 module origin mismatch')
        if name.startswith('build_only_v2.'):
            origin = getattr(module, '__file__', '')
            if not os.path.normcase(os.path.abspath(origin)).startswith(os.path.normcase(os.path.abspath(V2)) + os.sep):
                raise RuntimeError('v2 module origin mismatch')
    result = unittest.TextTestRunner(verbosity=2).run(suites)
    print('PURE_RESULT tests={} failures={} errors={} skipped={}'.format(
        result.testsRun, len(result.failures), len(result.errors), len(result.skipped)), flush=True)
    if not result.testsRun or not result.wasSuccessful() or result.skipped:
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
