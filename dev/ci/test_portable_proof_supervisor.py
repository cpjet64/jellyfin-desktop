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
    def executor(self):
        with patch.dict(sys.modules, {'resource': types.ModuleType('resource'), 'pwd': types.ModuleType('pwd')}):
            spec = importlib.util.spec_from_file_location('proof_executor', MODULE / 'executor.py')
            executor = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(executor)
        return executor

    def test_parent_partial_setup_closes_acquired_pipe(self):
        executor = self.executor()
        with patch.object(executor.os, 'pipe', side_effect=[(10, 11), OSError('pipe')]), \
             patch.object(executor.os, 'close') as close:
            with self.assertRaisesRegex(OSError, 'pipe'):
                executor.run_suite('python', ROOT, ROOT, 'suite', {}, 0, 'linux')
        self.assertEqual([call.args[0] for call in close.call_args_list], [10, 11])

    def test_parent_initial_mask_failure_closes_both_pipes(self):
        executor = self.executor()
        with patch.object(executor.os, 'pipe', side_effect=[(10, 11), (12, 13)]), \
             patch.object(executor.os, 'close') as close, \
             patch.object(executor.signal, 'pthread_sigmask', side_effect=OSError('mask'), create=True), \
             patch.object(executor.signal, 'SIG_BLOCK', 1, create=True), \
             patch.object(executor.signal, 'SIGALRM', 3, create=True):
            with self.assertRaisesRegex(OSError, 'mask'):
                executor.run_suite('python', ROOT, ROOT, 'suite', {}, 0, 'linux')
        self.assertEqual(set(call.args[0] for call in close.call_args_list), {10, 11, 12, 13})

    def test_parent_fork_failure_restores_mask_and_closes_pipes(self):
        executor = self.executor()
        with patch.object(executor.os, 'pipe', side_effect=[(10, 11), (12, 13)]), \
             patch.object(executor.os, 'close') as close, \
             patch.object(executor.os, 'fork', side_effect=OSError('fork'), create=True), \
             patch.object(executor.signal, 'pthread_sigmask', return_value={7}, create=True) as mask, \
             patch.object(executor.signal, 'SIG_BLOCK', 1, create=True), \
             patch.object(executor.signal, 'SIG_SETMASK', 2, create=True), \
             patch.object(executor.signal, 'SIGALRM', 3, create=True):
            with self.assertRaisesRegex(OSError, 'fork'):
                executor.run_suite('python', ROOT, ROOT, 'suite', {}, 0, 'linux')
        self.assertEqual(set(call.args[0] for call in close.call_args_list), {10, 11, 12, 13})
        self.assertEqual(mask.call_args_list[-1].args, (2, {7}))

    def test_parent_mask_and_stop_failures_do_not_skip_cleanup(self):
        executor = self.executor()
        mask = [set(), None, RuntimeError('block failed'), None]
        with patch.object(executor.os, 'pipe', side_effect=[(10, 11), (12, 13)]), \
             patch.object(executor.os, 'fork', return_value=42, create=True), \
             patch.object(executor.os, 'close') as close, \
             patch.object(executor.signal, 'pthread_sigmask', side_effect=mask, create=True), \
             patch.object(executor.signal, 'SIG_BLOCK', 1, create=True), \
             patch.object(executor.signal, 'SIG_SETMASK', 2, create=True), \
             patch.object(executor.signal, 'SIGALRM', 3, create=True), \
             patch.object(executor.select, 'select', side_effect=ValueError('primary')), \
             patch.object(executor, 'stop_supervisor', side_effect=TimeoutError('unreaped')) as stop:
            with self.assertRaisesRegex(ValueError, 'primary'):
                executor.run_suite('python', ROOT, ROOT, 'suite', {}, 0, 'linux')
        stop.assert_called_once_with(42, False)
        self.assertEqual(set(call.args[0] for call in close.call_args_list), {10, 11, 12, 13})
        self.assertIn('unreaped', executor.STATE['cleanup_unconfirmed'])

    def test_parent_cancellation_after_spawn_preserves_primary_and_settles(self):
        executor = self.executor()
        cancellation = KeyboardInterrupt('cancelled')
        with patch.object(executor.os, 'pipe', side_effect=[(10, 11), (12, 13)]), \
             patch.object(executor.os, 'fork', return_value=42, create=True), \
             patch.object(executor.os, 'close') as close, \
             patch.object(executor.os, 'read', return_value=b'R'), \
             patch.object(executor.os, 'write') as write, \
             patch.object(executor.signal, 'pthread_sigmask', return_value=set(), create=True), \
             patch.object(executor.signal, 'SIG_BLOCK', 1, create=True), \
             patch.object(executor.signal, 'SIG_SETMASK', 2, create=True), \
             patch.object(executor.signal, 'SIGALRM', 3, create=True), \
             patch.object(executor.select, 'select', side_effect=[([10], [], []), cancellation]), \
             patch.object(executor, 'stop_supervisor', side_effect=TimeoutError('unreaped')) as stop:
            with self.assertRaises(KeyboardInterrupt) as error:
                executor.run_suite('python', ROOT, ROOT, 'suite', {}, 0, 'linux')
        self.assertIs(error.exception, cancellation)
        stop.assert_called_once_with(42, True)
        write.assert_called_once_with(13, b'G')
        self.assertEqual(sorted(call.args[0] for call in close.call_args_list), [10, 11, 12, 13])
        self.assertIn('unreaped', executor.STATE['cleanup_unconfirmed'])

    def test_cleanup_failure_rejects_otherwise_successful_result(self):
        executor = self.executor()
        with patch.object(executor.os, 'pipe', side_effect=[(10, 11), (12, 13)]), \
             patch.object(executor.os, 'fork', return_value=42, create=True), \
             patch.object(executor.os, 'close') as close, \
             patch.object(executor.os, 'read', side_effect=[b'R', b'{"exit":0,"survivors":[]}']), \
             patch.object(executor.os, 'write'), \
             patch.object(executor.signal, 'pthread_sigmask', return_value=set(), create=True), \
             patch.object(executor.signal, 'SIG_BLOCK', 1, create=True), \
             patch.object(executor.signal, 'SIG_SETMASK', 2, create=True), \
             patch.object(executor.signal, 'SIGALRM', 3, create=True), \
             patch.object(executor.select, 'select', return_value=([10], [], [])), \
             patch.object(executor, 'stop_supervisor', side_effect=TimeoutError('unreaped')) as stop, \
             patch.object(executor, 'suite_log_result') as log:
            with self.assertRaisesRegex(RuntimeError, 'cleanup unconfirmed.*unreaped'):
                executor.run_suite('python', ROOT, ROOT, 'suite', {}, 0, 'linux')
        stop.assert_called_once_with(42, True)
        log.assert_not_called()
        self.assertEqual(sorted(call.args[0] for call in close.call_args_list), [10, 11, 12, 13])
        self.assertIn('unreaped', executor.STATE['cleanup_unconfirmed'])

    def test_bounded_reap_timeout_never_signals_again(self):
        executor = self.executor()
        with patch.object(executor.os, 'killpg', create=True) as killpg, \
             patch.object(executor.signal, 'SIGKILL', 9, create=True), \
             patch.object(executor.os, 'waitpid', return_value=(0, 0), create=True), \
             patch.object(executor.os, 'WNOHANG', 1, create=True), \
             patch.object(executor.time, 'monotonic', side_effect=[0, 5]):
            with self.assertRaisesRegex(TimeoutError, 'ownership retained'):
                executor.stop_supervisor(42, True)
            killpg.assert_called_once_with(42, 9)

    def test_no_signal_after_successful_reap(self):
        executor = self.executor()
        with patch.object(executor.os, 'killpg', side_effect=[None, ProcessLookupError()], create=True) as killpg, \
             patch.object(executor.signal, 'SIGKILL', 9, create=True), \
             patch.object(executor.os, 'waitpid', return_value=(42, 0), create=True), \
             patch.object(executor.os, 'WNOHANG', 1, create=True):
            executor.stop_supervisor(42, True)
        self.assertEqual([call.args for call in killpg.call_args_list], [(42, 9), (42, 0)])

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
