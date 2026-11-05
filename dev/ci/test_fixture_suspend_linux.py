"""Portable tests of native fixture decision code; no OS processes are signalled."""
import unittest
from types import SimpleNamespace
from unittest.mock import patch

import fixture_suspend_linux as native


def item(pid, parent, group, start, state='S'):
    return {'pid': pid, 'parent': parent, 'group': group, 'session': group, 'start': start, 'state': state}


def request(operation='snapshot'):
    return {'operation': operation, 'controller': 90, 'controller_group': 91,
            'registry': [item(42, 90, 42, 'leader'), item(43, 42, 42, 'child')],
            'control': item(100, 90, 100, 'control')}


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


if __name__ == '__main__':
    unittest.main()
