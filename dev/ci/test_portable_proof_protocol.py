import base64
import hashlib
import io
import json
import os
import subprocess
import types
import zlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).parent / 'portable_proof'))
import assembly
import controller
import commands
import transfer
import transport

class ProtocolTest(unittest.TestCase):
    def test_real_nested_transfer_and_bad_chunks(self):
        chunks = (b'first', b'second', b'last')
        pins = tuple((len(x), hashlib.sha256(x).hexdigest()) for x in chunks)
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp, patch.object(assembly, 'CHUNKS', pins), \
             patch.object(assembly, 'TOTAL', 15), patch.object(assembly, 'SHA256', hashlib.sha256(b''.join(chunks)).hexdigest()), \
             patch.object(assembly, 'validate_root'), patch.object(assembly, 'validate_workspace'), \
             patch.object(assembly.os, 'getuid', return_value=Path(tmp).stat().st_uid, create=True):
            task = 'a' * 32
            result = transfer.apply(tmp, task, 'init', [])
            with self.assertRaises((ValueError, OSError)):
                transfer.apply(tmp, task, 'append', ['5', result['sha256'], base64.b64encode(chunks[1]).decode()])
            for chunk in chunks:
                args = [str(result['offset']), result['sha256'], base64.b64encode(chunk).decode()]
                result = transfer.apply(tmp, task, 'append', args)
            self.assertEqual(transfer.apply(tmp, task, 'finish', [])['sha256'], assembly.SHA256)
            with self.assertRaises((FileExistsError, ValueError)):
                transfer.apply(tmp, task, 'finish', [])

    def test_request_limit_and_raw_terminal_capture(self):
        self.assertTrue(transport.request_body(['hello']))
        with self.assertRaises(ValueError):
            transport.request_body(['x' * 100001])
        raw = json.dumps({'data': {'exited': True, 'exitcode': 1}}).encode()
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            receipt = Path(tmp) / 'terminal.json'
            self.assertEqual(transport.poll(42, receipt, lambda: io.BytesIO(raw))['exitcode'], 1)
            self.assertEqual(receipt.read_bytes(), raw)
            self.assertEqual(transport.poll(42, receipt, lambda: self.fail('repoll'))['exitcode'], 1)

    def test_invalid_status_is_preserved_and_blocks_repoll(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            receipt = Path(tmp) / 'status.json'
            with self.assertRaises(ValueError):
                transport.poll(42, receipt, lambda: io.BytesIO(b'{bad'))
            self.assertEqual(receipt.read_bytes(), b'{bad')
            with self.assertRaises(Exception):
                transport.poll(42, receipt, lambda: self.fail('repoll'))

    def test_terminal_result_is_checked_through_journal_boundary(self):
        expected = {'offset': 0, 'sha256': hashlib.sha256(b'').hexdigest()}
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            root = Path(tmp)
            controller.submit_once(root, 0, 'init', [], {'id': 1}, lambda *args: 42)
            raw = json.dumps({'data': {'exited': True, 'exitcode': 0, 'out-data': json.dumps(expected)}}).encode()
            def poll(pid):
                return transport.terminal_result(pid, root / 'raw.json', lambda: io.BytesIO(raw))
            self.assertTrue(controller.settle_once(root, 0, {'id': 1}, poll, expected))
            self.assertEqual(json.loads((root / '0000.receipt.json').read_text()), expected)

    def test_terminal_partial_output_marks_failure_and_never_repolls(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            root = Path(tmp)
            controller.submit_once(root, 0, 'init', [], {'id': 1}, lambda *args: 42)
            raw = json.dumps({'data': {'exited': True, 'exitcode': 0, 'out-data': '{partial'}}).encode()
            def poll(pid):
                return transport.terminal_result(pid, root / 'raw.json', lambda: io.BytesIO(raw))
            with self.assertRaises(transport.TerminalFailure):
                controller.settle_once(root, 0, {'id': 1}, poll, {})
            self.assertTrue((root / '0000.failure.json').exists())
            with self.assertRaises(transport.TerminalFailure):
                transport.terminal_result(42, root / 'raw.json', lambda: self.fail('repoll'))

    def test_last_bad_helper_rejected_before_dynamic_execution(self):
        payload = {'assembly': "raise AssertionError('executed')", 'transfer': 'corrupt'}
        expected = {'assembly': hashlib.sha256(payload['assembly'].encode()).hexdigest(),
                    'transfer': hashlib.sha256(b'expected').hexdigest()}
        packed = base64.b64encode(zlib.compress(json.dumps(payload).encode())).decode()
        account = types.SimpleNamespace(pw_uid=123, pw_dir='unused')
        pwd = types.SimpleNamespace(getpwnam=lambda name: account)
        script = commands.BOOTSTRAP.replace('EXPECTED_PINS', repr(expected))
        with patch.dict(sys.modules, {'pwd': pwd}), patch('platform.system', return_value='Linux'), \
             patch('platform.machine', return_value='x86_64'), patch.object(assembly.os, 'getuid', return_value=123, create=True), \
             patch.object(assembly.os, 'geteuid', return_value=123, create=True), patch.object(sys, 'argv', ['-c', packed]):
            with self.assertRaisesRegex(RuntimeError, 'helper identity mismatch'):
                exec(script, {})

    def test_isolated_interpreter_rejects_invalid_helper_before_execution(self):
        payload = {'assembly': "raise AssertionError('executed')", 'transfer': 'bad'}
        expected = {'assembly': hashlib.sha256(payload['assembly'].encode()).hexdigest(),
                    'transfer': hashlib.sha256(b'expected').hexdigest()}
        packed = base64.b64encode(zlib.compress(json.dumps(payload).encode())).decode()
        shim = ('import sys,types,platform,os; platform.system=lambda:"Linux"; '
                'platform.machine=lambda:"x86_64"; os.getuid=lambda:123; os.geteuid=lambda:123; '
                'sys.modules["pwd"]=types.SimpleNamespace(getpwnam=lambda _:types.SimpleNamespace(pw_uid=123,pw_dir="unused"));\n')
        script = shim + commands.BOOTSTRAP.replace('EXPECTED_PINS', repr(expected))
        result = subprocess.run([sys.executable, '-I', '-S', '-c', script, packed], capture_output=True,
                                timeout=20, creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b'helper identity mismatch', result.stderr)
        self.assertNotIn(b'AssertionError: executed', result.stderr)

    def test_isolated_suite_runner_uses_only_explicit_checkout(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            root = Path(tmp)
            target = root / 'dev' / 'ci'
            target.mkdir(parents=True)
            for name in ('qga_terminal.py', 'test_qga_terminal.py'):
                (target / name).write_bytes((ROOT / 'dev' / 'ci' / name).read_bytes())
            runner = Path(__file__).parent / 'portable_proof' / 'suite_runner.py'
            result = subprocess.run([sys.executable, '-I', '-S', '-B', str(runner), str(root),
                                     'dev/ci/test_qga_terminal.py', 'linux'], capture_output=True, timeout=30,
                                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
            self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
            self.assertIn(b'W411_RESULT=', result.stdout)

    def test_unknown_submit_and_mixed_target_are_not_retried(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            root = Path(tmp)
            with self.assertRaises(RuntimeError):
                controller.submit_once(root, 0, 'init', [], {'id': 1}, lambda *args: (_ for _ in ()).throw(RuntimeError()))
            self.assertTrue((root / '0000.unknown.json').exists())
            with self.assertRaises(RuntimeError):
                controller.planned_step(root, ROOT / 'missing', {'id': 2}, plans=[])

    def test_planned_entry_is_bound_to_current_target(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            root = Path(tmp)
            controller.submit_once(root, 0, 'init', [], {'id': 1}, lambda *args: 42)
            with self.assertRaisesRegex(RuntimeError, 'target differs'):
                controller.planned_step(root, ROOT / 'missing', {'id': 2}, plans=[])

if __name__ == '__main__':
    unittest.main()
