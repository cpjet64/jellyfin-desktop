import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from dev.windows.proof.qualification_startup import NativeErrorMode, REQUESTED, quiet_error_ui


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
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()

    def test_zero_previous_and_restoration_evidence(self):
        api = FakeMode()
        with quiet_error_ui(self.root, api):
            self.assertEqual(api.mode, REQUESTED)
            record = json.loads((self.root / 'error-ui-start.json').read_text())
            self.assertEqual(record['previous_mask'], 0)
            self.assertTrue(record['wer_fault_dialog_suppression_requested'])
        self.assertEqual(api.mode, 0)
        self.assertEqual(json.loads((self.root / 'error-ui-restored.json').read_text())['restored_mask'], 0)

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
        self.assertFalse((self.root / 'error-ui-start.json').exists())
        self.assertFalse((self.root / 'error-ui-restored.json').exists())

    def test_body_failure_restores_without_restoration_record(self):
        api = FakeMode(0x40)
        with self.assertRaisesRegex(RuntimeError, 'body'):
            with quiet_error_ui(self.root, api):
                raise RuntimeError('body')
        self.assertEqual(api.mode, 0x40)
        self.assertTrue((self.root / 'error-ui-start.json').exists())
        self.assertFalse((self.root / 'error-ui-restored.json').exists())

    def test_existing_start_evidence_stops_and_restores(self):
        start = self.root / 'error-ui-start.json'
        start.write_bytes(b'preserve')
        api = FakeMode()
        with self.assertRaises(FileExistsError):
            with quiet_error_ui(self.root, api):
                self.fail('body must not run')
        self.assertEqual(api.mode, 0)
        self.assertEqual(start.read_bytes(), b'preserve')
        self.assertFalse((self.root / 'error-ui-restored.json').exists())

    def test_nonnative_constructor_rejects(self):
        with patch.object(os, 'name', 'posix'):
            with self.assertRaises(OSError):
                NativeErrorMode()


if __name__ == '__main__':
    unittest.main()
