"""Verify fixed raw helper pins against fresh, isolated Git materializations."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import Mock, patch

import macos_sdist_select_launch as launcher

ROOT = Path(__file__).resolve().parents[2]
FILES = {
    'dev/ci/macos_sdist_static.py': '95eee9ee01591ede2dbc9ecbedf79ec2d7c9239cc10ee61e5aa7cc5bce59d585',
    'dev/ci/macos_sdist_select.py': '9a08359840b1441c06fa2f1a192abf8f3db0b2114197da3dcb0b25439b0c6e9b',
    'dev/ci/macos_sdist_manifest.json': '0912faea678df3b5f6381a05784e1ab82050dc3d65c44c9c4a2f25dc4137a6bd',
}
HELPERS = tuple(FILES)[:2]
NO_WINDOW = getattr(subprocess, 'CREATE_NO_WINDOW', 0)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def command(args, cwd, env=None, check=True):
    return subprocess.run(args, cwd=cwd, env=env, capture_output=True, check=check,
                          timeout=30, creationflags=NO_WINDOW)


def git(root, *args, env=None):
    return command(['git', *args], root, env=env).stdout


def blob(path):
    return git(ROOT, 'show', 'HEAD:' + path)


def git_environment():
    env = os.environ.copy()
    for key in ('GIT_DIR', 'GIT_WORK_TREE', 'GIT_INDEX_FILE', 'GIT_COMMON_DIR'):
        env.pop(key, None)
    env.update(GIT_CONFIG_NOSYSTEM='1', GIT_CONFIG_GLOBAL=os.devnull, GIT_TERMINAL_PROMPT='0')
    return env


class PinTests(unittest.TestCase):
    def setUp(self):
        storage = ROOT / 'deps'
        storage.mkdir(exist_ok=True)
        if storage.is_symlink() or (hasattr(storage, 'is_junction') and storage.is_junction()) or storage.resolve() != storage:
            raise RuntimeError('test storage must remain inside the repository')
        self.temp = tempfile.TemporaryDirectory(prefix='w771-pins-', dir=storage)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.env = git_environment()

    def materialize(self, mode):
        root = self.root / mode
        root.mkdir()
        git(root, 'init', '--quiet', env=self.env)
        git(root, 'config', '--local', 'core.autocrlf', mode, env=self.env)
        (root / 'dev/ci').mkdir(parents=True)
        (root / '.gitattributes').write_bytes((ROOT / '.gitattributes').read_bytes())
        for path in FILES:
            (root / path).write_bytes(blob(path))
        git(root, 'add', '--', '.gitattributes', *FILES, env=self.env)
        git(root, 'checkout-index', '--all', '--force', env=self.env)
        return root

    def bootstrap(self, root):
        static, script = (root / path for path in HELPERS)
        args = [sys.executable, '-I', '-S', '-B', '-c', launcher.BOOTSTRAP, str(static), str(script),
                FILES[HELPERS[0]], FILES[HELPERS[1]], str(script), '--help']
        return command(args, root, check=False)

    def test_launcher_pins_equal_exact_committed_blobs(self):
        for path, expected in FILES.items():
            with self.subTest(path=path):
                self.assertEqual(launcher.PINS[ROOT / path], expected)
                self.assertEqual(digest(blob(path)), expected)

    def test_fresh_checkout_attributes_and_exact_blob_bytes(self):
        for mode in ('true', 'input', 'false'):
            with self.subTest(mode=mode):
                root = self.materialize(mode)
                for path, expected in FILES.items():
                    data = (root / path).read_bytes()
                    self.assertEqual(data, blob(path))
                    self.assertNotIn(b'\r\n', data)
                    self.assertEqual(digest(data), expected)
                    attrs = git(root, 'check-attr', '-z', 'text', 'eol', '--', path, env=self.env)
                    self.assertEqual(attrs.split(b'\0')[:-1],
                                     [path.encode(), b'text', b'set', path.encode(), b'eol', b'lf'])
                result = self.bootstrap(root)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(b'--source-dir', result.stdout)
                self.assertFalse((root / 'deps').exists())

    def test_existing_launcher_suite_on_fresh_checkout(self):
        root = self.materialize('true')
        for path in ('dev/ci/macos_sdist_select_launch.py', 'dev/ci/test_macos_sdist_select_launch.py'):
            (root / path).write_bytes((ROOT / path).read_bytes())
        result = command([sys.executable, '-B', 'dev/ci/test_macos_sdist_select_launch.py'], root, check=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b'Ran 10 tests', result.stderr)
        self.assertFalse((root / 'deps').exists())

    def test_changed_bytes_fail_before_bootstrap_or_parent_execution(self):
        root = self.materialize('false')
        for path in HELPERS:
            target = root / path
            original = target.read_bytes()
            variants = (original.replace(b'\n', b'\r\n'), original.replace(b'\n', b'\r\n', 1),
                        b'\xef\xbb\xbf' + original, original + b'\nprint("UNTRUSTED_EXECUTED")\n')
            for data in variants:
                with self.subTest(path=path, digest=digest(data)):
                    target.write_bytes(data)
                    result = self.bootstrap(root)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertEqual(result.stdout, b'')
                    self.assertIn(b'AssertionError', result.stderr)
                    self.assertNotIn(b'UNTRUSTED_EXECUTED', result.stdout + result.stderr)
                    with patch.dict(launcher.PINS, {target: FILES[path]}), \
                            patch('builtins.compile') as compile_call, patch('builtins.exec') as exec_call:
                        with self.assertRaisesRegex(ValueError, 'input hash mismatch'):
                            launcher.load_verified(target, 'untrusted_w771')
                    compile_call.assert_not_called()
                    exec_call.assert_not_called()
            target.write_bytes(original)

    def test_stale_populated_checkout_is_rejected(self):
        root = self.materialize('true')
        target = root / HELPERS[0]
        target.write_bytes(target.read_bytes().replace(b'\n', b'\r\n'))
        attrs = git(root, 'check-attr', 'eol', '--', HELPERS[0], env=self.env)
        self.assertIn(b'eol: lf', attrs)
        with patch.dict(launcher.PINS, {target: FILES[HELPERS[0]]}):
            with self.assertRaisesRegex(ValueError, 'input hash mismatch'):
                launcher.load_verified(target, 'stale_w771')
        self.assertNotEqual(self.bootstrap(root).returncode, 0)

    def test_preflight_checks_all_raw_inputs_before_loading_helpers(self):
        root = self.materialize('false')
        inputs = {root / name: expected for name, expected in FILES.items()}
        inputs[launcher.PYTHON] = launcher.PINS[launcher.PYTHON]
        actual_digest = launcher.digest
        selector = types.SimpleNamespace(ARCHIVES={}, validate_directory=Mock())
        def fake_digest(path):
            return inputs[path] if path == launcher.PYTHON else actual_digest(path)
        for path in FILES:
            target = root / path
            original = target.read_bytes()
            variants = (original.replace(b'\n', b'\r\n'), original.replace(b'\n', b'\r\n', 1),
                        b'\xef\xbb\xbf' + original, original + b'\n')
            for data in variants:
                with self.subTest(path=path, digest=digest(data)):
                    target.write_bytes(data)
                    try:
                        with patch.object(launcher, 'REPO', root), patch.object(launcher, 'OUTPUT', root / 'absent'), \
                                patch.object(launcher, 'PINS', inputs), patch.object(launcher.os, 'name', 'nt'), \
                                patch.object(launcher.shutil, 'disk_usage', return_value=types.SimpleNamespace(free=2**40)), \
                                patch.object(launcher, 'digest', side_effect=fake_digest), \
                                patch.object(launcher, 'load_verified', return_value=selector) as load:
                            with self.assertRaisesRegex(ValueError, 'input hash mismatch'):
                                launcher.preflight()
                            load.assert_not_called()
                    finally:
                        target.write_bytes(original)

    def test_preflight_accepts_pinned_bytes_with_injected_external_prerequisites(self):
        root = self.materialize('false')
        inputs = {root / name: expected for name, expected in FILES.items()}
        inputs[launcher.PYTHON] = launcher.PINS[launcher.PYTHON]
        actual_digest = launcher.digest
        selector = types.SimpleNamespace(ARCHIVES={}, validate_directory=Mock())
        def fake_digest(path):
            return inputs[path] if path == launcher.PYTHON else actual_digest(path)
        with patch.object(launcher, 'REPO', root), patch.object(launcher, 'OUTPUT', root / 'absent'), \
                patch.object(launcher, 'PINS', inputs), patch.object(launcher.os, 'name', 'nt'), \
                patch.object(launcher.shutil, 'disk_usage', return_value=types.SimpleNamespace(free=2**40)), \
                patch.object(launcher, 'digest', side_effect=fake_digest), patch.object(launcher, 'selector', None), \
                patch.object(launcher, 'load_verified', return_value=selector) as load:
            launcher.preflight()
            self.assertEqual(load.call_count, 2)
            self.assertEqual(selector.validate_directory.call_count, 2)


if __name__ == '__main__':
    unittest.main()
