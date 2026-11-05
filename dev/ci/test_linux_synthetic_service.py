"""Injected manager tests only; no native manager or process operations."""
import unittest

import linux_synthetic_service as subject

TASK = 'a' * 32
JOB = '/org/freedesktop/systemd1/job/42'


class FakeManager:
    def __init__(self):
        self.calls = []
        self.submit_error = None
        self.observe_error = None
        self.start_changes = {}
        self.capture_changes = {}
        self.capture_error = None
        self.stop_result = 'done'
        self.stop_error = None

    def submit(self, spec, deadline):
        self.calls.append(('submit', spec, deadline))
        if self.submit_error:
            raise self.submit_error
        return JOB

    def observe_start(self, unit, job, deadline):
        self.calls.append(('observe', unit, deadline))
        if self.observe_error:
            raise self.observe_error
        return {'unit': unit, 'job': job, 'invocation': 'b' * 32, 'cgroup': '/system.slice/' + unit,
                'reference_held': True, 'job_terminal': True, 'start_result': 'done', **self.start_changes}

    def capture(self, identity, deadline):
        self.calls.append(('capture', identity, deadline))
        if self.capture_error:
            raise self.capture_error
        stopped = any(call[0] == 'stop' for call in self.calls)
        result = {**identity, 'reference_held': True, 'state': 'inactive' if stopped else 'active',
                  'job_terminal': True, 'cgroup_empty': stopped, 'descendants_absent': stopped,
                  'evidence_saved': True}
        return {**result, **self.capture_changes}

    def stop(self, identity, deadline):
        self.calls.append(('stop', identity, deadline))
        if self.stop_error:
            raise self.stop_error
        return {**identity, 'stop_job': '/org/freedesktop/systemd1/job/43',
                'job_terminal': True, 'result': self.stop_result}


class SyntheticServiceTest(unittest.TestCase):
    def test_exact_synthetic_properties_and_successful_stop(self):
        backend = FakeManager()
        result = subject.SyntheticService(backend, clock=lambda: 100).run(TASK, 'stopped-child', True)
        spec = backend.calls[0][1]
        self.assertEqual(spec['unit'], 'w644-' + TASK + '.service')
        self.assertEqual(spec['properties'], {'Type': 'exec', 'User': '999', 'Group': '988', 'Restart': 'no',
                         'Delegate': False, 'KillMode': 'control-group', 'SendSIGKILL': True,
                         'RuntimeMaxSec': 25, 'TimeoutStartSec': 15, 'TimeoutStopSec': 15})
        self.assertEqual([call[0] for call in backend.calls], ['submit', 'observe', 'capture', 'stop', 'capture'])
        self.assertEqual(result['state'], 'inactive')

    def test_ordinary_completion_does_not_stop(self):
        backend = FakeManager()
        backend.capture_changes = {'state': 'inactive', 'cgroup_empty': True, 'descendants_absent': True}
        result = subject.SyntheticService(backend, clock=lambda: 100).run(TASK, 'ordinary', False)
        self.assertEqual(result['state'], 'inactive')
        self.assertNotIn('stop', [call[0] for call in backend.calls])

    def test_rejected_name_collision_exec_failure_and_missing_identity(self):
        for changes in ({'start_result': 'failed'}, {'invocation': ''}, {'cgroup': '/system.slice'},
                        {'reference_held': False}, {'job': JOB + '1'}):
            with self.subTest(changes=changes):
                backend = FakeManager()
                backend.start_changes = changes
                with self.assertRaises(subject.Unconfirmed):
                    subject.SyntheticService(backend, clock=lambda: 100).run(TASK, 'ordinary', True)
                expected = ['submit', 'observe', 'capture'] if changes == {'start_result': 'failed'} else ['submit', 'observe']
                self.assertEqual([call[0] for call in backend.calls], expected)
        for error in (subject.Rejected('rejected'), subject.Rejected('collision')):
            backend = FakeManager()
            backend.submit_error = error
            with self.assertRaises(subject.Rejected):
                subject.SyntheticService(backend, clock=lambda: 100).run(TASK, 'ordinary', True)
            self.assertEqual([call[0] for call in backend.calls], ['submit'])

    def test_lost_or_late_creation_never_resubmits_or_stops(self):
        for error in (TimeoutError('client timeout; job may start later'), ConnectionError('lost response')):
            backend = FakeManager()
            backend.submit_error = error
            runner = subject.SyntheticService(backend, clock=lambda: 100)
            with self.assertRaises(subject.Unconfirmed):
                runner.run(TASK, 'term-resistant', True)
            with self.assertRaises(subject.Unconfirmed):
                runner.run(TASK, 'term-resistant', True)
            self.assertEqual([call[0] for call in backend.calls], ['submit'])

    def test_late_start_return_prevents_observation_and_stop(self):
        backend = FakeManager()
        ticks = iter([0, 0, 0, 16])
        with self.assertRaisesRegex(subject.Unconfirmed, 'deadline expired'):
            subject.SyntheticService(backend, clock=lambda: next(ticks)).run(TASK, 'ordinary', True)
        self.assertEqual([call[0] for call in backend.calls], ['submit'])

    def test_lost_observations_and_missing_evidence_are_not_settlement(self):
        for changes in ({'state': 'missing'}, {'reference_held': False}, {'invocation': 'c' * 32}, {'evidence_saved': False},
                        {'state': 'inactive', 'cgroup_empty': True, 'descendants_absent': True, 'evidence_saved': False},
                        {'state': 'inactive', 'cgroup_empty': False, 'descendants_absent': False}):
            with self.subTest(changes=changes):
                backend = FakeManager()
                backend.capture_changes = changes
                with self.assertRaises(subject.Unconfirmed):
                    subject.SyntheticService(backend, clock=lambda: 100).run(TASK, 'session-changing', True)
                self.assertNotIn('stop', [call[0] for call in backend.calls])
        backend = FakeManager()
        backend.observe_error = ConnectionError('lost')
        with self.assertRaises(subject.Unconfirmed):
            subject.SyntheticService(backend, clock=lambda: 100).run(TASK, 'ordinary', True)
        self.assertEqual([call[0] for call in backend.calls], ['submit', 'observe'])

    def test_failed_stop_or_survivors_are_unconfirmed(self):
        for stop_result, changes in (('failed', {}), ('done', {'cgroup_empty': False}),
                                     ('done', {'descendants_absent': False})):
            backend = FakeManager()
            backend.stop_result = stop_result
            if changes:
                original = backend.capture
                def capture(identity, deadline):
                    result = original(identity, deadline)
                    return {**result, **changes} if result['state'] == 'inactive' else result
                backend.capture = capture
            with self.subTest(result=stop_result, changes=changes), self.assertRaises(subject.Unconfirmed):
                subject.SyntheticService(backend, clock=lambda: 100).run(TASK, 'term-resistant', True)
            self.assertEqual([call[0] for call in backend.calls].count('stop'), 1)

    def test_lost_stop_response_is_not_retried(self):
        backend = FakeManager()
        backend.stop_error = TimeoutError('manager may still stop')
        with self.assertRaisesRegex(subject.Unconfirmed, 'never retry'):
            subject.SyntheticService(backend, clock=lambda: 100).run(TASK, 'term-resistant', True)
        self.assertEqual([call[0] for call in backend.calls].count('stop'), 1)

    def test_stop_job_identity_must_match(self):
        backend = FakeManager()
        def stop(identity, deadline):
            backend.calls.append(('stop', identity, deadline))
            return {**identity, 'invocation': 'c' * 32, 'stop_job': '/org/freedesktop/systemd1/job/43',
                    'job_terminal': True, 'result': 'done'}
        backend.stop = stop
        with self.assertRaisesRegex(subject.Unconfirmed, 'stop job or identity'):
            subject.SyntheticService(backend, clock=lambda: 100).run(TASK, 'term-resistant', True)
        self.assertEqual([call[0] for call in backend.calls], ['submit', 'observe', 'capture', 'stop', 'capture'])

    def test_active_capture_must_preserve_evidence_before_stop(self):
        backend = FakeManager()
        backend.capture_changes = {'evidence_saved': False}
        with self.assertRaisesRegex(subject.Unconfirmed, 'pre-stop evidence'):
            subject.SyntheticService(backend, clock=lambda: 100).run(TASK, 'stopped-child', True)
        self.assertNotIn('stop', [call[0] for call in backend.calls])


if __name__ == '__main__':
    unittest.main()
