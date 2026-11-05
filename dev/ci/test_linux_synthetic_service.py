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
        self.mode = None
        self.cursor = 0

    def submit(self, spec, deadline):
        self.calls.append(('submit', spec, deadline))
        self.mode = spec['arguments'][-1]
        if self.submit_error:
            raise self.submit_error
        return JOB

    def observe_start(self, unit, job, deadline):
        self.calls.append(('observe', unit, deadline))
        if self.observe_error:
            raise self.observe_error
        return {'unit': unit, 'job': job, 'invocation': 'b' * 32, 'cgroup': '/system.slice/' + unit,
                'reference_held': True, 'bus': 'system', 'job_observed': True,
                'job_terminal': True, 'start_result': 'done', **self.start_changes}

    def capture(self, identity, deadline):
        self.calls.append(('capture', identity, deadline))
        if self.capture_error:
            raise self.capture_error
        stopped = any(call[0] == 'stop' for call in self.calls)
        result = {**identity, 'reference_held': True, 'state': 'inactive' if stopped else 'active',
                  'job_terminal': True, 'cgroup_empty': stopped, 'descendants_absent': stopped,
                  'evidence_saved': True, 'fixture_ready': True, 'fixture_mode': self.mode,
                  'fixture_result': 'passed' if stopped else None}
        self.cursor += 1
        return {**result, **self.capture_changes}, self.cursor

    def next_change(self, identity, cursor, deadline):
        return self.capture(identity, deadline)

    def submit_stop(self, identity, deadline):
        self.calls.append(('stop', identity, deadline))
        if self.stop_error:
            raise self.stop_error
        return '/org/freedesktop/systemd1/job/43'

    def observe_stop(self, identity, stop_job, deadline):
        self.calls.append(('observe_stop', identity, deadline))
        return {**identity, 'stop_job': stop_job,
                'observed_job': stop_job, 'observed_unit': identity['unit'],
                'job_observed': True, 'job_terminal': True, 'result': self.stop_result}

    def release(self, identity, snapshot, deadline):
        self.calls.append(('release', identity, deadline))


class SyntheticServiceTest(unittest.TestCase):
    def test_ordinary_waits_for_actual_completion(self):
        backend = FakeManager()
        clock = [100]
        original = backend.capture
        def capture(identity, deadline):
            clock[0] += 1
            snapshot, cursor = original(identity, deadline)
            if clock[0] == 102:
                snapshot.update(state='inactive', cgroup_empty=True, descendants_absent=True, fixture_result='passed')
            return snapshot, cursor
        backend.capture = capture
        result = subject.SyntheticService(backend, clock=lambda: clock[0]).run(TASK, 'ordinary', False)
        self.assertEqual(result['fixture_result'], 'passed')
        self.assertEqual([x[0] for x in backend.calls].count('capture'), 2)
        self.assertEqual([x[2] for x in backend.calls if x[0] == 'capture'], [125, 125])
        self.assertNotIn('stop', [x[0] for x in backend.calls])

    def test_ready_modes_wait_for_matching_fixture_ready(self):
        for mode in ('stopped-child', 'term-resistant', 'session-changing'):
            with self.subTest(mode=mode):
                backend = FakeManager()
                clock = [100]
                original = backend.capture
                def capture(identity, deadline):
                    clock[0] += 1
                    result, cursor = original(identity, deadline)
                    if result['state'] == 'active' and clock[0] == 101:
                        result['fixture_ready'] = False
                    return result, cursor
                backend.capture = capture
                subject.SyntheticService(backend, clock=lambda: clock[0]).run(TASK, mode, True)
                self.assertEqual([x[0] for x in backend.calls], ['submit', 'observe', 'capture', 'capture', 'stop', 'observe_stop', 'capture', 'release'])

    def test_readiness_deadline_retains_binding_without_stop(self):
        backend = FakeManager()
        clock = [100]
        original = backend.capture
        def capture(identity, deadline):
            clock[0] += 10
            result, cursor = original(identity, deadline)
            return {**result, 'fixture_ready': False}, cursor
        backend.capture = capture
        runner = subject.SyntheticService(backend, clock=lambda: clock[0])
        with self.assertRaises(subject.Unconfirmed) as raised:
            runner.run(TASK, 'stopped-child', True)
        self.assertEqual(raised.exception.last_known['submitted_job'], JOB)
        self.assertEqual(raised.exception.last_known['binding']['invocation'], 'b' * 32)
        self.assertNotIn('stop', [x[0] for x in backend.calls])
        with self.assertRaises(subject.Unconfirmed):
            runner.run(TASK, 'stopped-child', True)

    def test_rapid_completion_and_failed_fixture(self):
        for state, result, expected in (('inactive', 'passed', None), ('failed', 'failed', subject.FixtureFailed),
                                        ('inactive', 'failed', subject.FixtureFailed)):
            backend = FakeManager()
            backend.capture_changes = {'state': state, 'cgroup_empty': True, 'descendants_absent': True,
                                       'fixture_result': result}
            runner = subject.SyntheticService(backend, clock=lambda: 100)
            with self.subTest(state=state, result=result):
                if expected:
                    with self.assertRaises(expected):
                        runner.run(TASK, 'ordinary', False)
                else:
                    self.assertEqual(runner.run(TASK, 'ordinary', False)['fixture_result'], 'passed')

    def test_mismatched_readiness_or_lost_connection_never_stops(self):
        for changes, error in (({'fixture_mode': 'other'}, None), ({}, ConnectionError('bus lost'))):
            backend = FakeManager()
            backend.capture_changes = changes
            backend.capture_error = error
            with self.subTest(changes=changes, error=error), self.assertRaises(subject.Unconfirmed) as raised:
                subject.SyntheticService(backend, clock=lambda: 100).run(TASK, 'term-resistant', True)
            self.assertEqual(raised.exception.last_known['binding']['job'], JOB)
            self.assertNotIn('stop', [x[0] for x in backend.calls])

    def test_exact_synthetic_properties_and_successful_stop(self):
        backend = FakeManager()
        result = subject.SyntheticService(backend, clock=lambda: 100).run(TASK, 'stopped-child', True)
        spec = backend.calls[0][1]
        self.assertEqual(spec['unit'], 'w644-' + TASK + '.service')
        self.assertEqual(spec['manager'], {'bus': 'system', 'add_ref': True, 'watch_jobs_before_start': True})
        self.assertEqual(spec['properties'], {'Type': 'exec', 'User': '999', 'Group': '988', 'Restart': 'no',
                         'Delegate': False, 'KillMode': 'control-group', 'SendSIGKILL': True,
                         'RuntimeMaxSec': 24, 'TimeoutStartSec': 10, 'TimeoutStopSec': 8})
        self.assertEqual([call[0] for call in backend.calls], ['submit', 'observe', 'capture', 'stop', 'observe_stop', 'capture', 'release'])
        self.assertEqual(result['state'], 'inactive')

    def test_ordinary_completion_does_not_stop(self):
        backend = FakeManager()
        backend.capture_changes = {'state': 'inactive', 'cgroup_empty': True, 'descendants_absent': True,
                                   'fixture_result': 'passed'}
        result = subject.SyntheticService(backend, clock=lambda: 100).run(TASK, 'ordinary', False)
        self.assertEqual(result['state'], 'inactive')
        self.assertNotIn('stop', [call[0] for call in backend.calls])

    def test_rejected_name_collision_exec_failure_and_missing_identity(self):
        for changes in ({'start_result': 'failed'}, {'invocation': ''}, {'cgroup': '/system.slice'},
                        {'reference_held': False}, {'job_observed': False}, {'bus': 'direct'}, {'job': JOB + '1'}):
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
                    result, cursor = original(identity, deadline)
                    return ({**result, **changes} if result['state'] == 'inactive' else result), cursor
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

    def test_late_stop_reply_retains_returned_job_before_deadline_check(self):
        backend = FakeManager()
        clock = [100]
        original = backend.submit_stop
        def submit_stop(identity, deadline):
            result = original(identity, deadline)
            clock[0] += 16
            return result
        backend.submit_stop = submit_stop
        runner = subject.SyntheticService(backend, clock=lambda: clock[0])
        with self.assertRaises(subject.Unconfirmed) as raised:
            runner.run(TASK, 'stopped-child', True)
        self.assertTrue(raised.exception.last_known['stop_attempted'])
        self.assertEqual(raised.exception.last_known['stop_job'], '/org/freedesktop/systemd1/job/43')
        self.assertNotIn('observe_stop', [x[0] for x in backend.calls])
        with self.assertRaises(subject.Unconfirmed):
            runner.run(TASK, 'stopped-child', True)
        self.assertEqual([x[0] for x in backend.calls].count('stop'), 1)

    def test_stop_job_identity_must_match(self):
        backend = FakeManager()
        def observe_stop(identity, stop_job, deadline):
            backend.calls.append(('observe_stop', identity, deadline))
            return {**identity, 'invocation': 'c' * 32, 'stop_job': '/org/freedesktop/systemd1/job/43',
                    'observed_job': '/org/freedesktop/systemd1/job/43', 'observed_unit': identity['unit'],
                    'job_observed': True, 'job_terminal': True, 'result': 'done'}
        backend.observe_stop = observe_stop
        with self.assertRaisesRegex(subject.Unconfirmed, 'stop job or identity'):
            subject.SyntheticService(backend, clock=lambda: 100).run(TASK, 'term-resistant', True)
        self.assertEqual([call[0] for call in backend.calls], ['submit', 'observe', 'capture', 'stop', 'observe_stop', 'capture'])

    def test_stop_observation_must_correlate_returned_job(self):
        backend = FakeManager()
        original = backend.observe_stop
        def observe_stop(identity, stop_job, deadline):
            return {**original(identity, stop_job, deadline), 'observed_job': '/org/freedesktop/systemd1/job/99'}
        backend.observe_stop = observe_stop
        with self.assertRaisesRegex(subject.Unconfirmed, 'stop job or identity') as raised:
            subject.SyntheticService(backend, clock=lambda: 100).run(TASK, 'term-resistant', True)
        self.assertEqual(raised.exception.last_known['stop_job'], '/org/freedesktop/systemd1/job/43')
        self.assertTrue(raised.exception.last_known['stop_attempted'])
        self.assertEqual([x[0] for x in backend.calls].count('stop'), 1)

    def test_self_consistent_wrong_stop_job_is_rejected(self):
        backend = FakeManager()
        original = backend.observe_stop
        def observe_stop(identity, stop_job, deadline):
            wrong = '/org/freedesktop/systemd1/job/99'
            return {**original(identity, stop_job, deadline), 'stop_job': wrong, 'observed_job': wrong}
        backend.observe_stop = observe_stop
        with self.assertRaisesRegex(subject.Unconfirmed, 'stop job or identity') as raised:
            subject.SyntheticService(backend, clock=lambda: 100).run(TASK, 'term-resistant', True)
        self.assertEqual(raised.exception.last_known['stop_job'], '/org/freedesktop/systemd1/job/43')

    def test_nonadvancing_watch_cursor_is_unconfirmed(self):
        backend = FakeManager()
        backend.capture_changes = {'fixture_ready': False}
        original = backend.capture
        def no_change(identity, cursor, deadline):
            snapshot, _ = original(identity, deadline)
            return snapshot, cursor
        backend.next_change = no_change
        with self.assertRaisesRegex(subject.Unconfirmed, 'cursor'):
            subject.SyntheticService(backend, clock=lambda: 100).run(TASK, 'stopped-child', True)
        self.assertNotIn('stop', [call[0] for call in backend.calls])

    def test_late_next_change_cannot_trigger_stop(self):
        backend = FakeManager()
        backend.capture_changes = {'fixture_ready': False}
        clock = [100]
        original = backend.next_change
        def late(identity, cursor, deadline):
            value = original(identity, cursor, deadline)
            clock[0] = deadline
            return value
        backend.next_change = late
        with self.assertRaisesRegex(subject.Unconfirmed, 'deadline expired'):
            subject.SyntheticService(backend, clock=lambda: clock[0]).run(TASK, 'stopped-child', True)
        self.assertNotIn('stop', [call[0] for call in backend.calls])

    def test_active_capture_must_preserve_evidence_before_stop(self):
        backend = FakeManager()
        backend.capture_changes = {'evidence_saved': False}
        with self.assertRaisesRegex(subject.Unconfirmed, 'fixture evidence'):
            subject.SyntheticService(backend, clock=lambda: 100).run(TASK, 'stopped-child', True)
        self.assertNotIn('stop', [call[0] for call in backend.calls])


if __name__ == '__main__':
    unittest.main()
