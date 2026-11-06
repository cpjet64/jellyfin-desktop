import hashlib
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from portable_proof import assembly

ROOT = Path(__file__).resolve().parents[2]
TASK = 'a' * 32
DATA = (b'one', b'two', b'three')

class AssemblyTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=ROOT)
        self.addCleanup(self.temp.cleanup)
        helper = patch.object(assembly, 'validate_root')
        helper.start()
        self.addCleanup(helper.stop)
        self.path = assembly.workspace(self.temp.name, TASK)
        chunks = tuple((len(x), hashlib.sha256(x).hexdigest()) for x in DATA)
        patches = (patch.object(assembly, 'CHUNKS', chunks), patch.object(assembly, 'TOTAL', 11),
                   patch.object(assembly, 'SHA256', hashlib.sha256(b''.join(DATA)).hexdigest()),
                   patch.object(os, 'getuid', return_value=self.path.stat().st_uid, create=True))
        for item in patches:
            item.start()
            self.addCleanup(item.stop)
        if os.name == 'nt':
            def validate(path, task):
                if path != self.path or task != TASK:
                    raise ValueError('task identity mismatch')
            helper = patch.object(assembly, 'validate_workspace', side_effect=validate)
            helper.start()
            self.addCleanup(helper.stop)

    def test_complete_and_duplicate_rejected(self):
        for index, data in enumerate(DATA):
            assembly.append(self.path, TASK, index, data)
        self.assertEqual(assembly.finish(self.path, TASK).read_bytes(), b''.join(DATA))
        with self.assertRaises((FileExistsError, ValueError)):
            assembly.append(self.path, TASK, 0, DATA[0])
        with self.assertRaises((FileExistsError, ValueError)):
            assembly.finish(self.path, TASK)

    def test_missing_reordered_corrupt_and_mixed_task(self):
        with self.assertRaises((ValueError, OSError)):
            assembly.append(self.path, TASK, 1, DATA[1])
        with self.assertRaises(ValueError):
            assembly.append(self.path, TASK, 0, b'bad')
        with self.assertRaises(ValueError):
            assembly.append(self.path, 'b' * 32, 0, DATA[0])
        assembly.append(self.path, TASK, 0, DATA[0])
        (self.path / 'chunk-0').write_bytes(b'bad')
        with self.assertRaises(ValueError):
            assembly.append(self.path, TASK, 1, DATA[1])
        with self.assertRaises((ValueError, OSError)):
            assembly.finish(self.path, TASK)

    def test_unexpected_layout_is_rejected(self):
        (self.path / 'unexpected').write_bytes(b'')
        with self.assertRaisesRegex(ValueError, 'layout'):
            assembly.append(self.path, TASK, 0, DATA[0])
        (self.path / 'unexpected').unlink()
        for index, data in enumerate(DATA):
            assembly.append(self.path, TASK, index, data)
        (self.path / 'unexpected').write_bytes(b'')
        with self.assertRaisesRegex(ValueError, 'layout'):
            assembly.finish(self.path, TASK)

if __name__ == '__main__':
    unittest.main()
