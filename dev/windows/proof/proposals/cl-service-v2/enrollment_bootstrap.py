"""Fixed source origins for externally pinned v2 enrollment children."""
import importlib.util
import json
import os
import sys

V1_PROOF = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-fixture-detached-20261114/dev/windows/proof'
V2_PROOF = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/compiler-contract-v2-review-20261114/dev/windows/proof'
BASE = V2_PROOF + '/proposals/cl-service-v2'
ROOT = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-build-policy-20261113/deps/enrollment-cl-claim-20261114-01'
PRIVATE = 'E:/CursorAI/JellyfinDesktop/deps/reconcile-20261113/build-metadata-01/env-private/environment-private.json'
PRIVATE_HASH = '1daa8974e1d37694f1067c92ab9839bab22d2f8a1d3fa844e0b2b58ea0c4512a'


def _same(left, right):
    return os.path.normcase(os.path.abspath(left)) == os.path.normcase(os.path.abspath(right))


def _unique(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate metadata key')
        result[key] = value
    return result


def setup(cache_name):
    cache = ROOT + '/' + cache_name
    if (sys.platform != 'win32' or not sys.flags.isolated or not sys.flags.no_site
            or not sys.dont_write_bytecode or not sys.pycache_prefix or not _same(sys.pycache_prefix, cache)):
        raise RuntimeError('fixed isolated bootstrap flags required')
    if not os.path.isdir(cache) or os.listdir(cache):
        raise RuntimeError('fresh empty bytecode prefix required')
    sys.path.insert(0, V2_PROOF)
    sys.path.insert(0, V1_PROOF)


def verify_origins():
    for name, module in tuple(sys.modules.items()):
        if name == 'build_only_v2':
            locations = tuple(getattr(module, '__path__', ()))
            if len(locations) != 1 or not _same(locations[0], V2_PROOF + '/build_only_v2'):
                raise RuntimeError('v2 package origin mismatch')
            continue
        if name == 'build_only' or name.startswith('build_only.'):
            filename = '__init__.py' if name == 'build_only' else name.split('.')[-1] + '.py'
            expected = V1_PROOF + '/build_only/' + filename
        elif name.startswith('build_only_v2.'):
            expected = V2_PROOF + '/build_only_v2/' + name.split('.')[-1] + '.py'
        else:
            continue
        if not _same(getattr(module, '__file__', ''), expected):
            raise RuntimeError('project module origin mismatch')


def profile(endpoint, source_digest):
    from build_only.identity import FilePin
    from build_only.winapi import WinAPI
    api = WinAPI()
    with FilePin(api, PRIVATE, PRIVATE_HASH) as pin:
        if pin.size > 1048576:
            raise ValueError('private snapshot exceeds bound')
        with open(PRIVATE, 'rb') as stream:
            captured = json.load(stream, object_pairs_hook=_unique)
        pin.assert_path_unchanged()
    if api.opened != api.closed or api.close_failed:
        raise RuntimeError('private snapshot handles did not close')
    path = BASE + '/enrollment_profile.py'
    spec = importlib.util.spec_from_file_location('fixed_enrollment_profile', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    if not _same(module.__file__, path):
        raise RuntimeError('profile builder origin mismatch')
    result = module.build_profile(endpoint, captured, source_digest)
    verify_origins()
    return result
