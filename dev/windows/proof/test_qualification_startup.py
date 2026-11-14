import os
from pathlib import Path
import unittest
from unittest.mock import patch

import qualification_startup
from qualification_startup import NativeErrorMode, REQUESTED, quiet_error_ui


class FakeMode:
    def __init__(self, mode=0):
        self.mode = mode
        self.calls = []
        self.fail_readback = False

    def get_error_mode(self):
        if self.fail_readback and len(self.calls) == 1:
            return 0
        return self.mode

    def set_error_mode(self, mode):
        previous = self.mode
        self.mode = mode
        self.calls.append(mode)
        return previous


class QuietErrorUITests(unittest.TestCase):
    def setUp(self):
        project = Path(__file__).resolve().parents[3]
        self.root = project / 'deps' / 'qualification-startup-unit'
        self.records = {}
        writer = patch.object(qualification_startup, '_write_evidence', side_effect=self.write_record)
        directory = patch.object(Path, 'is_dir', return_value=True)
        writer.start()
        directory.start()
        self.addCleanup(writer.stop)
        self.addCleanup(directory.stop)

    def write_record(self, path, record):
        if path in self.records:
            raise FileExistsError(str(path))
        self.records[path] = dict(record)

    def test_zero_previous_and_restoration_evidence(self):
        api = FakeMode()
        with quiet_error_ui(self.root, api):
            self.assertEqual(api.mode, REQUESTED)
            record = self.records[self.root / 'error-ui-start.json']
            self.assertEqual(record['previous_mask'], 0)
            self.assertTrue(record['wer_invocation_suppression_requested'])
            self.assertEqual(record['scope'], 'disposable-fixture-qualification')
        self.assertEqual(api.mode, 0)
        self.assertEqual(self.records[self.root / 'error-ui-restored.json']['restored_mask'], 0)

    def test_existing_bits_are_preserved(self):
        api = FakeMode(0x40)
        with quiet_error_ui(self.root, api):
            self.assertEqual(api.mode, 0x40 | REQUESTED)
        self.assertEqual(api.mode, 0x40)

    def test_readback_rejection_restores_without_evidence(self):
        api = FakeMode()
        api.fail_readback = True
        with self.assertRaises(OSError):
            with quiet_error_ui(self.root, api):
                self.fail('body must not run')
        self.assertEqual(api.mode, 0)
        self.assertNotIn(self.root / 'error-ui-start.json', self.records)
        self.assertNotIn(self.root / 'error-ui-restored.json', self.records)

    def test_body_failure_restores_without_restoration_record(self):
        api = FakeMode(0x40)
        with self.assertRaisesRegex(RuntimeError, 'body'):
            with quiet_error_ui(self.root, api):
                raise RuntimeError('body')
        self.assertEqual(api.mode, 0x40)
        self.assertIn(self.root / 'error-ui-start.json', self.records)
        self.assertNotIn(self.root / 'error-ui-restored.json', self.records)

    def test_existing_start_evidence_stops_and_restores(self):
        start = self.root / 'error-ui-start.json'
        self.records[start] = {'existing': 'preserve'}
        api = FakeMode()
        with self.assertRaises(FileExistsError):
            with quiet_error_ui(self.root, api):
                self.fail('body must not run')
        self.assertEqual(api.mode, 0)
        self.assertEqual(self.records[start], {'existing': 'preserve'})
        self.assertNotIn(self.root / 'error-ui-restored.json', self.records)

    def test_nonnative_constructor_rejects(self):
        with patch.object(os, 'name', 'posix'):
            with self.assertRaises(OSError):
                NativeErrorMode()


if __name__ == '__main__':
    unittest.main()
