"""Deterministic tests of the actual portable-proof root policy."""
import os
from pathlib import Path
import stat
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from portable_proof import assembly

UID = 1234
LIMIT = 2 * 1024**3
ROOT = Path(__file__).resolve().parent / 'policy-root'
TASK = 'a' * 32


def entry(uid=UID, mode=0o755):
    return SimpleNamespace(st_uid=uid, st_mode=stat.S_IFDIR | mode)


class RootPolicyTest(unittest.TestCase):
    def setUp(self):
        self.entries = {item: entry() for item in (ROOT, *ROOT.parents)}
        self.patches = (
            patch.object(os, 'getuid', return_value=UID, create=True),
            patch.object(Path, 'resolve', lambda path, strict: path),
            patch.object(Path, 'lstat', lambda path: self.entries[path]),
            patch.object(Path, 'stat', lambda path: self.entries[path]),
            patch.object(assembly.shutil, 'disk_usage', return_value=SimpleNamespace(free=LIMIT)),
        )
        for item in self.patches:
            item.start()
            self.addCleanup(item.stop)

    def test_canonical_safe_root_and_root_owned_ancestor(self):
        self.entries[ROOT.parent] = entry(uid=0)
        assembly.validate_root(ROOT)

    def test_relative_and_noncanonical_or_symlink_resolution_rejected(self):
        with self.assertRaisesRegex(ValueError, 'noncanonical'):
            assembly.validate_root(Path('relative'))
        with patch.object(Path, 'resolve', return_value=ROOT.parent):
            with self.assertRaisesRegex(ValueError, 'noncanonical'):
                assembly.validate_root(ROOT)

    def test_unsafe_entries_rejected_at_root_and_ancestor(self):
        for location in (ROOT, ROOT.parent):
            for value in (SimpleNamespace(st_uid=UID, st_mode=stat.S_IFREG | 0o644),
                          SimpleNamespace(st_uid=UID, st_mode=stat.S_IFLNK | 0o777),
                          entry(uid=UID + 1), entry(mode=0o775), entry(mode=0o757)):
                with self.subTest(location=str(location), value=value):
                    original = self.entries[location]
                    self.entries[location] = value
                    with self.assertRaisesRegex(ValueError, 'unsafe root ancestor'):
                        assembly.validate_root(ROOT)
                    self.entries[location] = original

    def test_root_itself_must_be_owned(self):
        self.entries[ROOT] = entry(uid=0)
        with self.assertRaisesRegex(ValueError, 'unowned or full'):
            assembly.validate_root(ROOT)

    def test_free_space_boundary(self):
        with patch.object(assembly.shutil, 'disk_usage', return_value=SimpleNamespace(free=LIMIT - 1)):
            with self.assertRaisesRegex(ValueError, 'unowned or full'):
                assembly.validate_root(ROOT)
        assembly.validate_root(ROOT)

    def test_filesystem_errors_propagate(self):
        for target, method in ((Path, 'resolve'), (Path, 'lstat'), (Path, 'stat'),
                               (assembly.shutil, 'disk_usage')):
            with self.subTest(method=method), patch.object(target, method, side_effect=OSError('failure')):
                with self.assertRaisesRegex(OSError, 'failure'):
                    assembly.validate_root(ROOT)

    def test_workspace_validates_before_mkdir(self):
        with patch.object(assembly, 'validate_root', side_effect=ValueError('blocked')) as validate, \
             patch.object(Path, 'mkdir') as mkdir:
            with self.assertRaisesRegex(ValueError, 'blocked'):
                assembly.workspace(ROOT, TASK)
            validate.assert_called_once_with(ROOT)
            mkdir.assert_not_called()

    def test_workspace_identity_and_private_directory_policy(self):
        path = ROOT / ('portable-proof-' + TASK)
        self.entries[path] = entry(mode=0o700)
        assembly.validate_workspace(path, TASK)
        for wrong_path, wrong_task in ((path, 'b' * 32), (ROOT / 'other', TASK)):
            with self.assertRaisesRegex(ValueError, 'task identity'):
                assembly.validate_workspace(wrong_path, wrong_task)
        for value in (SimpleNamespace(st_uid=UID, st_mode=stat.S_IFREG | 0o600),
                      SimpleNamespace(st_uid=UID, st_mode=stat.S_IFLNK | 0o777),
                      entry(uid=UID + 1, mode=0o700), entry(mode=0o710)):
            with self.subTest(value=value):
                self.entries[path] = value
                with self.assertRaisesRegex(ValueError, 'unsafe workspace'):
                    assembly.validate_workspace(path, TASK)
        self.entries[path] = entry(mode=0o700)
        self.entries[ROOT.parent] = entry(mode=0o777)
        with self.assertRaisesRegex(ValueError, 'unsafe root ancestor'):
            assembly.validate_workspace(path, TASK)

    def test_append_and_finish_validate_workspace(self):
        with patch.object(assembly, 'validate_workspace', side_effect=ValueError('blocked')) as validate:
            for operation in (lambda: assembly.append(ROOT, TASK, 0, b''),
                              lambda: assembly.finish(ROOT, TASK)):
                with self.assertRaisesRegex(ValueError, 'blocked'):
                    operation()
            self.assertEqual(validate.call_count, 2)


if __name__ == '__main__':
    unittest.main()
