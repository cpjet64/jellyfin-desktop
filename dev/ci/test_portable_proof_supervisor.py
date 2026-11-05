import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
MODULE = Path(__file__).parent / 'portable_proof'
sys.path.insert(0, str(MODULE))

class SupervisorTest(unittest.TestCase):
    def test_success_failure_timeout_and_survivors(self):
        with patch.dict(sys.modules, {'resource': types.ModuleType('resource'), 'pwd': types.ModuleType('pwd')}):
            spec = importlib.util.spec_from_file_location('proof_executor', MODULE / 'executor.py')
            executor = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(executor)
        for outcome, survivors in ((0, []), (1, []), (0, [123]), (subprocess.TimeoutExpired('x', 1), [])):
            writes = []
            class Child:
                def wait(self, timeout):
                    if isinstance(outcome, Exception):
                        raise outcome
                    return outcome
            with self.subTest(outcome=outcome, survivors=survivors), tempfile.TemporaryDirectory(dir=ROOT) as tmp, \
                 patch.object(executor.os, 'setsid', create=True), patch.object(executor.os, 'read', return_value=b'G'), \
                 patch.object(executor.os, 'write', side_effect=lambda fd, data: writes.append(data)), \
                 patch.object(executor.os, '_exit', side_effect=SystemExit), \
                 patch.object(executor.signal, 'pause', side_effect=SystemExit, create=True), \
                 patch.object(executor.subprocess, 'Popen', return_value=Child()), \
                 patch.object(executor, 'group_survivors', return_value=survivors):
                with self.assertRaises(SystemExit):
                    executor.run_supervisor(1, 2, 'python', Path(tmp), Path(tmp), 'suite', {}, 'linux', Path(tmp) / 'log')
            self.assertEqual(writes[0], b'R')
            result = json.loads(writes[1])
            if isinstance(outcome, Exception):
                self.assertEqual(result['error'], 'TimeoutExpired')
            else:
                self.assertEqual(result, {'exit': outcome, 'survivors': survivors})
            if outcome == 0 and not survivors:
                executor.check_supervisor_result(result, 'suite')
            else:
                with self.assertRaisesRegex(RuntimeError, 'failed or left descendants'):
                    executor.check_supervisor_result(result, 'suite')

if __name__ == '__main__':
    unittest.main()
