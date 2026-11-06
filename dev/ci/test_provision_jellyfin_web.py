"""Offline tests of the fixed, bounded producer source acquisition gate."""
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import Mock, patch

import provision_jellyfin_web as subject


class ProvisionTests(unittest.TestCase):
    def setUp(self):
        storage = subject.ROOT / 'deps'
        storage.mkdir(exist_ok=True)
        if storage.is_symlink() or (hasattr(storage, 'is_junction') and storage.is_junction()) or storage.resolve() != storage:
            raise RuntimeError('test storage is not repository-local')
        self.temp = tempfile.TemporaryDirectory(prefix='w783-source-', dir=storage)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.path = self.root / 'deps/ci-jellyfin-web/playbackmanager.js'
        self.data = b'approved source fixture\n'
        self.root_patch = patch.object(subject, 'ROOT', self.root)
        self.size_patch = patch.object(subject, 'SIZE', len(self.data))
        self.sha_patch = patch.object(subject, 'SHA', hashlib.sha256(self.data).hexdigest())
        for item in (self.root_patch, self.size_patch, self.sha_patch):
            item.start()
            self.addCleanup(item.stop)

    def test_new_and_existing_cache_are_verified_each_time(self):
        fetch = Mock(return_value=self.data)
        self.assertEqual(subject.provision(self.path, fetch), self.path)
        self.assertEqual(subject.provision(self.path, fetch), self.path)
        fetch.assert_called_once()
        self.path.write_bytes(b'bad')
        with self.assertRaises(RuntimeError):
            subject.provision(self.path, fetch)
        fetch.assert_called_once()
        self.assertEqual(self.path.read_bytes(), b'bad')

    def test_invalid_download_never_publishes_or_runs_node(self):
        for data in (b'', self.data[:-1], self.data + b'x', b'x' * (subject.LIMIT + 1),
                     b'x' * len(self.data)):
            with self.subTest(size=len(data)), self.assertRaises(RuntimeError):
                subject.provision(self.path, lambda: data)
            self.assertFalse(self.path.exists())
        node = Mock()
        with self.assertRaises(FileNotFoundError):
            subject.report(self.path, node)
        node.assert_not_called()

    def test_acquisition_and_deadline_failure_leave_final_absent(self):
        for error in (OSError('acquisition failed'), subprocess.TimeoutExpired('curl', 60)):
            with self.subTest(error=type(error).__name__), self.assertRaises(type(error)):
                subject.provision(self.path, Mock(side_effect=error))
            self.assertFalse(self.path.exists())

    def test_download_uses_fixed_url_cap_and_total_timeout(self):
        run = Mock(side_effect=[Mock(stdout=b'curl 8.4.0 test'), Mock(stdout=self.data)])
        self.assertEqual(subject.download(run, clock=lambda: 0), self.data)
        args, options = run.call_args_list[1]
        self.assertEqual(args[0][-1], subject.URL)
        self.assertEqual(args[0][args[0].index('--max-filesize') + 1], str(256 * 1024))
        self.assertEqual(args[0][args[0].index('--max-time') + 1], '60')
        self.assertEqual(options['timeout'], 60)
        self.assertEqual(args[0][1], '--disable')
        with self.assertRaises(RuntimeError):
            subject.download(Mock(return_value=Mock(stdout=b'curl 8.3.0 test')))

    def test_invalid_cache_prevents_every_child(self):
        subject.provision(self.path, lambda: self.data)
        self.path.write_bytes(b'x' * len(self.data))
        run = Mock()
        with self.assertRaises(RuntimeError):
            subject.report(self.path, run)
        run.assert_not_called()

    def test_verified_source_report_contains_provenance(self):
        subject.provision(self.path, lambda: self.data)
        run = Mock(return_value=Mock(stdout='a' * 40 + '\n'))
        with patch.object(subject, 'normalized_hash', return_value='b' * 64):
            subject.report(self.path, run)
        run.assert_called_once()
        self.assertEqual(run.call_args.args[0], ['git', 'rev-parse', 'HEAD'])

    def test_transfer_budget_includes_version_check(self):
        run = Mock(side_effect=[Mock(stdout=b'curl 8.4.0 test'), Mock(stdout=self.data)])
        self.assertEqual(subject.download(run, clock=iter((10, 25)).__next__), self.data)
        self.assertEqual(run.call_args.kwargs['timeout'], 45)
        run.reset_mock(side_effect=True, return_value=True)
        run.return_value = Mock(stdout=b'curl 8.4.0 test')
        with self.assertRaises(subprocess.TimeoutExpired):
            subject.download(run, clock=iter((10, 70)).__next__)
        self.assertEqual(run.call_count, 1)

    def test_linked_cache_and_existing_file_are_not_replaced(self):
        self.path.parent.mkdir(parents=True)
        external = self.root / 'other'
        external.write_bytes(self.data)
        try:
            self.path.symlink_to(external)
        except OSError as error:
            self.skipTest('symlink creation unavailable: ' + type(error).__name__)
        fetch = Mock(return_value=self.data)
        with self.assertRaises(RuntimeError):
            subject.provision(self.path, fetch)
        fetch.assert_not_called()
        self.assertEqual(external.read_bytes(), self.data)

    def test_hardlinked_cache_is_rejected(self):
        self.path.parent.mkdir(parents=True)
        external = self.root / 'other'
        external.write_bytes(self.data)
        os.link(external, self.path)
        fetch = Mock(return_value=self.data)
        with self.assertRaisesRegex(RuntimeError, 'regular file'):
            subject.provision(self.path, fetch)
        fetch.assert_not_called()
        self.assertEqual(external.read_bytes(), self.data)

    def test_detected_reparse_path_is_rejected_before_fetch(self):
        with patch.object(Path, 'is_symlink', return_value=True):
            fetch = Mock()
            with self.assertRaisesRegex(RuntimeError, 'linked cache path'):
                subject.provision(self.path, fetch)
        fetch.assert_not_called()

    def test_failed_atomic_publish_never_overwrites_final(self):
        self.path.parent.mkdir(parents=True)
        with patch.object(subject.os, 'link', side_effect=FileExistsError('occupied')):
            with self.assertRaises(FileExistsError):
                subject.provision(self.path, lambda: self.data)
        self.assertFalse(self.path.exists())
        self.assertEqual(list(self.path.parent.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
