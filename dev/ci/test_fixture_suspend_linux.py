"""Portable tests of native fixture decision code; no OS processes are signalled."""
import unittest
import time
from types import SimpleNamespace
from unittest.mock import Mock, patch

import fixture_suspend_linux as native


def item(pid, parent, group, start, state='S'):
    return {'pid': pid, 'parent': parent, 'group': group, 'session': group, 'start': start, 'state': state}


def request(operation='snapshot'):
    return {'operation': operation, 'controller': 90, 'controller_group': 91,
            'registry': [item(42, 90, 42, 'leader'), item(43, 42, 42, 'child')],
            'control': item(100, 90, 100, 'control'), 'deadline': time.monotonic() + 10}


class NativeDecisionTest(unittest.TestCase):
    def setUp(self):
        self.records = {42: item(42, 90, 42, 'leader'), 43: item(43, 42, 42, 'child'),
                        100: item(100, 90, 100, 'control')}

    def test_snapshot_and_stop_use_exact_owned_group(self):
        with patch.object(native, 'verify_proc'), patch.object(native.os, 'getppid', return_value=90), \
                patch.object(native, 'proc_stat', side_effect=lambda pid: dict(self.records[pid])), \
                patch.object(native, 'process_set', side_effect=lambda: dict(self.records)), \
                patch.object(native.signal, 'SIGSTOP', 19, create=True), \
                patch.object(native.os, 'killpg', create=True) as kill:
            snapshot = native.probe(request())
            self.assertTrue(snapshot['descendants_complete'])
            self.assertTrue(snapshot['control_unchanged'])
            self.assertTrue(native.probe(request('stop'))['sent'])
            kill.assert_called_once_with(42, native.signal.SIGSTOP)

    def test_unknown_descendant_and_control_change_prevent_signal(self):
        for mutate in ('escape', 'control'):
            records = {pid: dict(value) for pid, value in self.records.items()}
            if mutate == 'escape':
                records[44] = item(44, 43, 999, 'escaped')
            else:
                records[100]['state'] = 'T'
            with patch.object(native, 'verify_proc'), patch.object(native.os, 'getppid', return_value=90), \
                    patch.object(native, 'proc_stat', side_effect=lambda pid: dict(records[pid])), \
                    patch.object(native, 'process_set', side_effect=lambda: dict(records)), \
                    patch.object(native.signal, 'SIGSTOP', 19, create=True), \
                    patch.object(native.os, 'killpg', create=True) as kill:
                with self.assertRaises(ValueError):
                    native.probe(request('stop'))
                kill.assert_not_called()

    def test_unissued_fixture_never_reaches_native_helper(self):
        ops = native.Operations(native.Launcher())
        with patch.object(native, 'helper') as helper:
            with self.assertRaises((ValueError, AttributeError)):
                ops.stop(object(), (), 1)
            helper.assert_not_called()

    def test_delayed_probe_cannot_signal_after_deadline(self):
        req = request('stop')
        with patch.object(native, 'verify_proc'), patch.object(native.os, 'getppid', return_value=90), \
                patch.object(native, 'proc_stat', side_effect=lambda pid: dict(self.records[pid])), \
                patch.object(native, 'process_set', side_effect=lambda: dict(self.records)), \
                patch.object(native.time, 'monotonic', return_value=req['deadline']), \
                patch.object(native.os, 'killpg', create=True) as kill:
            with self.assertRaises(TimeoutError):
                native.probe(req)
            kill.assert_not_called()

    def test_invalid_signal_deadline_is_rejected(self):
        for deadline in (float('nan'), float('inf'), True):
            with self.assertRaises(ValueError):
                native.remaining(deadline)

    def test_teardown_deadline_boundaries(self):
        for expiry in ('settled', 'leader_wait', 'control_wait', None):
            with self.subTest(expiry=expiry):
                clock = [100.0]
                leader = Mock(pid=42)
                control = Mock()
                identity = SimpleNamespace(pid=100, start='control', parent=90)
                fixture = SimpleNamespace(leader=leader, control=control, control_identity=identity)
                registry = (SimpleNamespace(pid=42), SimpleNamespace(pid=43, start='child'))
                def run(args, **_kwargs):
                    if '--settled' in args:
                        if expiry == 'settled':
                            clock[0] = 101.0
                        return SimpleNamespace(returncode=0, stdout='settled')
                    return SimpleNamespace(returncode=0)
                def leader_wait(**_kwargs):
                    if expiry == 'leader_wait':
                        clock[0] = 101.0
                def control_wait(**_kwargs):
                    if expiry == 'control_wait':
                        clock[0] = 101.0
                leader.wait.side_effect = leader_wait
                control.wait.side_effect = control_wait
                ops = native.Operations(native.Launcher())
                with patch.object(ops, '_check'), patch.object(native, 'helper', return_value={'sent': True}), \
                        patch.object(native.time, 'monotonic', side_effect=lambda: clock[0]), \
                        patch.object(native.subprocess, 'run', side_effect=run) as process, \
                        patch.object(native.signal, 'SIGCHLD', 17, create=True), \
                        patch.object(native.signal, 'getsignal', return_value=native.signal.SIG_DFL), \
                        patch.object(native.os, 'P_PID', 1, create=True), \
                        patch.object(native.os, 'WEXITED', 4, create=True), \
                        patch.object(native.os, 'WNOHANG', 1, create=True), \
                        patch.object(native.os, 'WNOWAIT', 8, create=True), \
                        patch.object(native.os, 'waitid', return_value=SimpleNamespace(si_pid=42), create=True):
                    if expiry is None:
                        self.assertTrue(ops.teardown(fixture, registry, 1))
                        self.assertEqual(process.call_count, 2)
                        control.wait.assert_called_once()
                    else:
                        with self.assertRaises(TimeoutError):
                            ops.teardown(fixture, registry, 1)
                        self.assertEqual(process.call_count, 1 if expiry in ('settled', 'leader_wait') else 2)

    def test_control_signal_checks_deadline_after_identity(self):
        with patch.object(native, 'proc_stat', return_value={'start': 'control', 'parent': 90}), \
                patch.object(native.os, 'getppid', return_value=90), \
                patch.object(native.time, 'monotonic', return_value=101.0), \
                patch.object(native.os, 'kill') as kill:
            with self.assertRaises(TimeoutError):
                native.stop_control(100, 'control', 90, 101.0)
            kill.assert_not_called()

    def test_partial_launch_retains_unreaped_owned_handle(self):
        leader = SimpleNamespace(pid=42, stdout=object())
        class Selector:
            def register(self, _stream, _event):
                pass
            def select(self, _timeout):
                return []
            def close(self):
                pass
        with patch.object(native.sys, 'platform', 'linux'), \
                patch.object(native.signal, 'SIGCHLD', 17, create=True), \
                patch.object(native.signal, 'getsignal', return_value=native.signal.SIG_DFL), \
                patch.object(native.subprocess, 'Popen', return_value=leader), \
                patch.object(native.selectors, 'DefaultSelector', return_value=Selector()), \
                patch.object(native.os, 'killpg', create=True) as kill:
            launcher = native.Launcher()
            with self.assertRaises(native.FixtureLaunchError) as error:
                launcher.create(1)
            self.assertIs(error.exception.leader, leader)
            self.assertIsNone(error.exception.child_pid)
            kill.assert_not_called()
            with self.assertRaises(ValueError):
                launcher.create(1)

    def test_invalid_deadlines_do_not_launch(self):
        with patch.object(native.sys, 'platform', 'linux'), \
                patch.object(native.signal, 'SIGCHLD', 17, create=True), \
                patch.object(native.signal, 'getsignal', return_value=native.signal.SIG_DFL), \
                patch.object(native.subprocess, 'Popen') as launch:
            for value in (float('nan'), float('inf'), -1, 0, True, '1'):
                with self.assertRaises(ValueError):
                    native.Launcher().create(value)
            launch.assert_not_called()

    def test_launch_steps_share_deadline(self):
        clock = [100.0]
        leader = SimpleNamespace(pid=42, stdout=SimpleNamespace(fileno=lambda: 7))
        class Selector:
            def register(self, _stream, _event):
                pass
            def select(self, timeout):
                self.timeout = timeout
                clock[0] += 0.6
                return [1]
            def close(self):
                pass
        def stat(*_args, **kwargs):
            self.assertAlmostEqual(kwargs['timeout'], 0.4)
            clock[0] += 0.5
            return SimpleNamespace(stdout='{"start":"leader","parent":90,"group":42,"session":42}')
        with patch.object(native.sys, 'platform', 'linux'), \
                patch.object(native.signal, 'SIGCHLD', 17, create=True), \
                patch.object(native.signal, 'getsignal', return_value=native.signal.SIG_DFL), \
                patch.object(native.time, 'monotonic', side_effect=lambda: clock[0]), \
                patch.object(native.subprocess, 'Popen', return_value=leader) as launch, \
                patch.object(native.subprocess, 'run', side_effect=stat) as run, \
                patch.object(native.selectors, 'DefaultSelector', return_value=Selector()), \
                patch.object(native.os, 'read', return_value=b'{"child":43}'):
            with self.assertRaises(native.FixtureLaunchError) as error:
                native.Launcher().create(1)
            self.assertIsInstance(error.exception.__cause__, TimeoutError)
            self.assertEqual(error.exception.child_pid, 43)
            launch.assert_called_once()
            run.assert_called_once()


if __name__ == '__main__':
    unittest.main()
