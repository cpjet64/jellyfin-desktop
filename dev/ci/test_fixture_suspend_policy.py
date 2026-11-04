"""Portable, injected contract tests; no operating-system processes are signalled."""
import errno
import unittest

from fixture_suspend_policy import Member, Observation, Policy, Result


def sample(states=('R', 'R'), **changes):
    data = {'group': 42, 'leader': 42, 'lease': 'owned', 'leader_held': True,
            'visible': True, 'descendants_complete': True, 'escaped': False,
            'members': {42: Member('leader-start', 42, states[0]), 43: Member('child-start', 42, states[1])}}
    data.update(changes)
    return Observation(**data)


class Backend:
    def __init__(self, observations, stop_error=None):
        self.observations = list(observations)
        self.stop_error = stop_error
        self.signals = []
        self.clock = 0

    def observe_without_reaping(self):
        value = self.observations.pop(0)
        if isinstance(value, BaseException):
            raise value
        return value

    def send_stop(self, group):
        self.signals.append(group)
        if self.stop_error:
            raise self.stop_error
        return True

    def now(self):
        self.clock += 1
        return self.clock


class FixturePolicyTest(unittest.TestCase):
    def policy(self, backend):
        return Policy(leader=42, group=42, controller_pid=90, controller_group=91,
                      lease='owned', backend=backend, max_samples=3, deadline=20)

    def test_guest_enospc_requires_actual_complete_stopped_observation(self):
        backend = Backend([sample(), sample(('T', 'R')), sample(('T', 'T'))])
        result = self.policy(backend).storage_error('guest', OSError(errno.ENOSPC, 'full'))
        self.assertEqual(result, Result.STOPPED)
        self.assertEqual(backend.signals, [42])

    def test_controller_storage_and_nonstorage_never_signal(self):
        for origin, error in [('controller', OSError(errno.ENOSPC, 'full')), ('guest', OSError(errno.EIO, 'io'))]:
            backend = Backend([])
            self.assertNotEqual(self.policy(backend).storage_error(origin, error), Result.STOPPED)
            self.assertEqual(backend.signals, [])

    def test_injected_edquot_only_where_errno_exists(self):
        if not hasattr(errno, 'EDQUOT'):
            self.skipTest('platform does not define EDQUOT')
        backend = Backend([sample(), sample(('T', 'T'))])
        self.assertEqual(self.policy(backend).storage_error('guest', OSError(errno.EDQUOT, 'quota')), Result.STOPPED)

    def test_invalid_and_foreign_targets_rejected(self):
        backend = Backend([])
        for values in ((0, 0, 90, 91), (1, 1, 90, 91), (42, 43, 90, 91), (42, 42, 90, 42)):
            with self.assertRaises(ValueError):
                Policy(*values, lease='owned', backend=backend, max_samples=3, deadline=20)
        self.assertEqual(backend.signals, [])

    def test_partial_signal_failed_or_invisible_is_unproven(self):
        cases = [(Backend([sample()], PermissionError()), Result.UNPROVEN),
                 (Backend([sample(), sample(('T', 'R')), sample(('T', 'R')), sample(('T', 'R'))]), Result.UNPROVEN),
                 (Backend([sample(visible=False)]), Result.UNPROVEN),
                 (Backend([sample(), sample(escaped=True)]), Result.UNPROVEN)]
        for backend, expected in cases:
            self.assertEqual(self.policy(backend).storage_error('guest', OSError(errno.ENOSPC, 'full')), expected)

    def test_loss_or_reaped_leader_invalidates_future_signal(self):
        for first in (sample(leader_held=False), OSError('observation lost')):
            backend = Backend([first])
            policy = self.policy(backend)
            self.assertEqual(policy.storage_error('guest', OSError(errno.ENOSPC, 'full')), Result.UNPROVEN)
            self.assertEqual(policy.storage_error('guest', OSError(errno.ENOSPC, 'full')), Result.UNPROVEN)
            self.assertEqual(backend.signals, [])

    def test_identity_reuse_and_unrelated_process_are_not_signalled(self):
        changed = sample(members={42: Member('leader-start', 42, 'T'), 43: Member('different-start', 42, 'T')})
        backend = Backend([sample(), changed])
        self.assertEqual(self.policy(backend).storage_error('guest', OSError(errno.ENOSPC, 'full')), Result.UNPROVEN)
        self.assertEqual(backend.signals, [42])
        self.assertNotIn(91, backend.signals)

    def test_leader_exit_or_post_signal_observation_loss_never_retries(self):
        for after in (sample(('Z', 'T')), OSError('visibility lost'), sample(leader_held=False)):
            backend = Backend([sample(), after])
            policy = self.policy(backend)
            self.assertEqual(policy.storage_error('guest', OSError(errno.ENOSPC, 'full')), Result.UNPROVEN)
            self.assertEqual(policy.storage_error('guest', OSError(errno.ENOSPC, 'full')), Result.UNPROVEN)
            self.assertEqual(backend.signals, [42])

    def test_failed_signal_return_cannot_claim_stopped(self):
        backend = Backend([sample()])
        backend.send_stop = lambda group: False
        self.assertEqual(self.policy(backend).storage_error('guest', OSError(errno.ENOSPC, 'full')), Result.UNPROVEN)

    def test_expired_deadline_never_signals(self):
        for deadline in (1, 2):
            backend = Backend([sample()])
            policy = Policy(42, 42, 90, 91, 'owned', backend, 3, deadline)
            self.assertEqual(policy.storage_error('guest', OSError(errno.ENOSPC, 'full')), Result.UNPROVEN)
            self.assertEqual(backend.signals, [])

    def test_wrapped_storage_error_and_cycles(self):
        wrapped = RuntimeError('wrapper')
        wrapped.__cause__ = OSError(errno.ENOSPC, 'full')
        backend = Backend([sample(), sample(('T', 'T'))])
        self.assertEqual(self.policy(backend).storage_error('guest', wrapped), Result.STOPPED)
        unrelated = RuntimeError('cycle')
        unrelated.__cause__ = unrelated
        backend = Backend([])
        self.assertEqual(self.policy(backend).storage_error('guest', unrelated), Result.IGNORED)
        self.assertEqual(backend.signals, [])

    def test_unknown_origin_and_unreadable_errno_are_unproven(self):
        backend = Backend([])
        policy = self.policy(backend)
        self.assertEqual(policy.storage_error('unknown', OSError(errno.ENOSPC, 'full')), Result.UNPROVEN)
        self.assertEqual(policy.storage_error('guest', OSError(errno.ENOSPC, 'full')), Result.UNPROVEN)
        self.assertEqual(backend.signals, [])
        class BadErrno(OSError):
            @property
            def errno(self):
                raise RuntimeError('unreadable')
        self.assertEqual(self.policy(Backend([])).storage_error('guest', BadErrno()), Result.UNPROVEN)

    def test_nonfinite_clock_and_sample_limit_fail_closed(self):
        backend = Backend([])
        backend.now = lambda: float('nan')
        self.assertEqual(self.policy(backend).storage_error('guest', OSError(errno.ENOSPC, 'full')), Result.UNPROVEN)
        self.assertEqual(backend.signals, [])
        with self.assertRaises(ValueError):
            Policy(42, 42, 90, 91, 'owned', backend, 129, 20)

    def test_observation_finishing_after_deadline_is_unproven(self):
        class SlowBackend(Backend):
            def observe_without_reaping(self):
                value = super().observe_without_reaping()
                if self.signals:
                    self.clock = 100
                return value
        backend = SlowBackend([sample(), sample(('T', 'T'))])
        self.assertEqual(self.policy(backend).storage_error('guest', OSError(errno.ENOSPC, 'full')), Result.UNPROVEN)
        self.assertEqual(backend.signals, [42])

    def test_explicit_observation_loss_forbids_later_signal(self):
        backend = Backend([])
        policy = self.policy(backend)
        self.assertEqual(policy.observation_lost(), Result.UNPROVEN)
        self.assertEqual(policy.storage_error('guest', OSError(errno.ENOSPC, 'full')), Result.UNPROVEN)
        self.assertEqual(backend.signals, [])


if __name__ == '__main__':
    unittest.main()
