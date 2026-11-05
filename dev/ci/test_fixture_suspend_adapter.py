"""Portable adapter tests with injected fixture operations; no native process launch."""
import errno
import unittest
from types import SimpleNamespace

from fixture_suspend_adapter import FixtureAdapter, FixtureIdentity
from fixture_suspend_policy import Result


class Launcher:
    def create(self, _timeout):
        return SimpleNamespace(controller_pid=90, controller_group=91, identities=(
            FixtureIdentity(42, 'leader', 90, 42, 42), FixtureIdentity(43, 'child', 42, 42, 42)))


class Operations:
    def __init__(self):
        self.records = []
        self.members = {42: {'start': 'leader', 'parent': 90, 'group': 42, 'session': 42, 'state': 'R'},
                        43: {'start': 'child', 'parent': 42, 'group': 42, 'session': 42, 'state': 'R'}}
        self.fail = None

    def snapshot(self, fixture, registry, timeout):
        self.records.append(('snapshot', tuple(registry), timeout))
        if self.fail == 'snapshot':
            raise TimeoutError('injected')
        return {'members': self.members, 'leader_held': True, 'visible': True,
                'descendants_complete': True, 'escaped': False, 'control_unchanged': True}

    def stop(self, fixture, registry, timeout):
        self.records.append(('stop', tuple(registry), timeout))
        if self.fail == 'stop':
            raise TimeoutError('injected')
        for member in self.members.values():
            member['state'] = 'T'
        return True

    def teardown(self, fixture, registry, timeout):
        self.records.append(('teardown', tuple(registry), timeout))
        if self.fail == 'teardown-exception':
            raise TimeoutError('injected')
        return self.fail != 'teardown'


class AdapterTest(unittest.TestCase):
    def test_snapshots_copy_mutable_registry_state(self):
        ops = Operations()
        adapter = FixtureAdapter(Launcher(), ops)
        first = adapter.observe_without_reaping()
        ops.members[43]['start'] = 'changed'
        self.assertEqual(first.members[43].identity, 'child')
        self.assertEqual(adapter.storage_error('guest', OSError(errno.ENOSPC, 'full')), Result.UNPROVEN)

    def test_stop_and_fixture_only_teardown(self):
        ops = Operations()
        adapter = FixtureAdapter(Launcher(), ops)
        self.assertEqual(adapter.storage_error('guest', OSError(errno.ENOSPC, 'full')), Result.STOPPED)
        self.assertTrue(adapter.teardown())
        self.assertTrue(adapter.teardown())
        self.assertEqual([entry[0] for entry in ops.records].count('stop'), 1)
        self.assertEqual([entry[0] for entry in ops.records].count('teardown'), 1)
        self.assertTrue(all({item.pid for item in row[1]} == {42, 43} for row in ops.records))

    def test_observation_loss_or_timeout_never_resignals(self):
        for kind in ('snapshot', 'stop'):
            ops = Operations()
            ops.fail = kind
            adapter = FixtureAdapter(Launcher(), ops)
            self.assertEqual(adapter.storage_error('guest', OSError(errno.ENOSPC, 'full')), Result.UNPROVEN)
            self.assertEqual(adapter.storage_error('guest', OSError(errno.ENOSPC, 'full')), Result.UNPROVEN)
            self.assertFalse(adapter.teardown())
            self.assertLessEqual([entry[0] for entry in ops.records].count('stop'), 1)

    def test_explicit_loss_never_signals_or_tears_down_by_old_identity(self):
        ops = Operations()
        adapter = FixtureAdapter(Launcher(), ops)
        self.assertEqual(adapter.observation_lost(), Result.UNPROVEN)
        self.assertEqual(adapter.storage_error('guest', OSError(errno.ENOSPC, 'full')), Result.UNPROVEN)
        self.assertFalse(adapter.teardown())
        self.assertEqual(ops.records, [])

    def test_lost_control_or_escaped_child_is_unproven(self):
        for changed in ({'control_unchanged': False}, {'escaped': True}, {'descendants_complete': False}):
            ops = Operations()
            original = ops.snapshot
            def modified(*args):
                return dict(original(*args), **changed)
            ops.snapshot = modified
            adapter = FixtureAdapter(Launcher(), ops)
            self.assertEqual(adapter.storage_error('guest', OSError(errno.ENOSPC, 'full')), Result.UNPROVEN)
            self.assertFalse(any(record[0] == 'stop' for record in ops.records))

    def test_teardown_failure_is_not_retried(self):
        for failure in ('teardown', 'teardown-exception'):
            ops = Operations()
            ops.fail = failure
            adapter = FixtureAdapter(Launcher(), ops)
            self.assertFalse(adapter.teardown())
            self.assertFalse(adapter.teardown())
            self.assertEqual([entry[0] for entry in ops.records].count('teardown'), 1)

    def test_direct_signal_is_not_an_authorized_policy_transition(self):
        ops = Operations()
        adapter = FixtureAdapter(Launcher(), ops)
        with self.assertRaises(RuntimeError):
            adapter.send_stop(42)
        self.assertEqual(ops.records, [])

    def test_nonfinite_deadline_is_rejected_before_launch(self):
        class NeverLaunch:
            def create(self, _timeout):
                raise AssertionError('must not start fixture')
        with self.assertRaises(ValueError):
            FixtureAdapter(NeverLaunch(), Operations(), float('nan'))


if __name__ == '__main__':
    unittest.main()
