"""Fake-client systemd mapping and watched-event tests; no bus is opened."""
import unittest

import linux_synthetic_service as model
import linux_synthetic_service_adapter as adapter

TASK = 'a' * 32
START = '/org/freedesktop/systemd1/job/42'
STOP = '/org/freedesktop/systemd1/job/43'


class BufferedClient:
    transport = 'system-bus'

    def __init__(self):
        self.calls = []
        self.jobs = []
        self.cursor = 0
        self.unit = None
        self.stopped = False
        self.capture_count = 0
        self.lost_reference = False

    def subscribe(self, deadline):
        self.calls.append(('subscribe', deadline))
        return True

    def watch_jobs(self, deadline):
        self.calls.append(('watch_jobs', deadline))
        return len(self.jobs)

    def watch_unit(self, unit, deadline):
        # Contract: install a name-filtered signal watch; do not load the unit.
        self.calls.append(('watch_unit', unit))
        self.unit = unit
        return True

    def start_transient(self, unit, mode, properties, auxiliary, deadline):
        self.calls.append(('start', unit, mode, properties, auxiliary))
        self.jobs.append({'job': START, 'unit': unit, 'terminal': True, 'result': 'done'})
        return START  # JobRemoved is buffered before the method reply.

    def wait_job(self, cursor, job, deadline):
        self.calls.append(('wait_job', cursor, job))
        return next(item for item in self.jobs[cursor:] if item['job'] == job)

    def unit_identity(self, unit, deadline):
        return {'invocation': 'b' * 32, 'cgroup': '/system.slice/' + unit,
                'reference_held': not self.lost_reference}

    def unit_cursor(self, unit, deadline):
        self.calls.append(('cursor', self.cursor))
        return self.cursor

    def capture_evidence(self, unit, cursor, deadline):
        self.calls.append(('capture', self.cursor))
        self.capture_count += 1
        if self.capture_count == 1:
            self.cursor += 1  # A change arrives after the cursor but during capture.
        active = not self.stopped
        return {'unit': unit, 'job': START, 'invocation': 'b' * 32, 'cgroup': '/system.slice/' + unit,
                'watch_cursor': cursor, 'provenance': 'independent-observer', 'observation_revision': self.capture_count,
                'reference_held': not self.lost_reference, 'state': 'active' if active else 'inactive',
                'job_terminal': True, 'cgroup_empty': not active, 'descendants_absent': not active,
                'evidence_saved': True, 'fixture_ready': self.capture_count > 1, 'fixture_mode': 'stopped-child',
                'fixture_result': None if active else 'passed'}

    def wait_unit_change(self, unit, cursor, deadline):
        self.calls.append(('wait_change', cursor))
        if self.cursor <= cursor:
            raise TimeoutError('no buffered change')
        return self.cursor

    def job_cursor(self, deadline):
        self.calls.append(('job_cursor', len(self.jobs)))
        return len(self.jobs)

    def stop_unit(self, unit, mode, deadline):
        self.calls.append(('stop', unit, mode))
        self.stopped = True
        self.jobs.append({'job': STOP, 'unit': unit, 'terminal': True, 'result': 'done'})
        return STOP  # Stop JobRemoved is buffered before its reply too.

    def unref_unit(self, unit, deadline):
        self.calls.append(('unref', unit))


class AdapterTest(unittest.TestCase):
    def test_typed_transient_properties_and_precise_time_units(self):
        spec = model.synthetic_spec(TASK, 'stopped-child')
        properties = {name: (signature, value) for name, signature, value in adapter.typed_properties(spec)}
        self.assertEqual(properties['AddRef'], ('b', True))
        self.assertEqual(properties['TimeoutStartUSec'], ('t', 10_000_000))
        self.assertEqual(properties['TimeoutStopUSec'], ('t', 8_000_000))
        self.assertEqual(properties['RuntimeMaxUSec'], ('t', 24_000_000))
        self.assertEqual(properties['ExecStart'][0], 'a(sasb)')
        self.assertNotIn('TimeoutStopSec', properties)

    def test_job_before_reply_and_change_between_snapshot_and_wait(self):
        client = BufferedClient()
        result = model.SyntheticService(adapter.SystemdAdapter(client), clock=lambda: 100).run(TASK, 'stopped-child', True)
        self.assertEqual(result['state'], 'inactive')
        names = [call[0] for call in client.calls]
        self.assertLess(names.index('subscribe'), names.index('start'))
        self.assertLess(names.index('watch_jobs'), names.index('start'))
        self.assertLess(names.index('watch_unit'), names.index('start'))
        self.assertLess(names.index('cursor'), names.index('capture'))
        self.assertIn(('wait_change', 0), client.calls)
        self.assertIn(('wait_job', 0, START), client.calls)
        self.assertIn(('wait_job', 1, STOP), client.calls)
        self.assertEqual(names[-1], 'unref')

    def test_reference_loss_never_settles_or_releases(self):
        client = BufferedClient()
        client.lost_reference = True
        with self.assertRaises(model.Unconfirmed):
            model.SyntheticService(adapter.SystemdAdapter(client), clock=lambda: 100).run(TASK, 'stopped-child', True)
        self.assertNotIn('stop', [call[0] for call in client.calls])
        self.assertNotIn('unref', [call[0] for call in client.calls])

    def test_fresh_observation_rejects_changed_or_stale_evidence_before_action(self):
        for field, bad in (('invocation', 'c' * 32), ('cgroup', '/other'),
                           ('reference_held', False), ('observation_revision', 1),
                           ('fixture_mode', 'ordinary'), ('watch_cursor', -1)):
            for capture_number, forbidden in ((3, 'stop'), (5, 'unref')):
                with self.subTest(field=field, capture=capture_number):
                    client = BufferedClient()
                    original = client.capture_evidence
                    def capture(unit, cursor, deadline):
                        value = original(unit, cursor, deadline)
                        if client.capture_count == capture_number:
                            value[field] = bad
                        return value
                    client.capture_evidence = capture
                    with self.assertRaises(model.Unconfirmed):
                        model.SyntheticService(adapter.SystemdAdapter(client), clock=lambda: 100).run(
                            TASK, 'stopped-child', True)
                    self.assertNotIn(forbidden, [call[0] for call in client.calls])

    def test_changed_completion_before_release_is_unconfirmed(self):
        client = BufferedClient()
        original = client.capture_evidence
        def capture(unit, cursor, deadline):
            value = original(unit, cursor, deadline)
            if client.capture_count == 5:
                value['fixture_result'] = 'failed'
            return value
        client.capture_evidence = capture
        with self.assertRaises(model.Unconfirmed):
            model.SyntheticService(adapter.SystemdAdapter(client), clock=lambda: 100).run(TASK, 'stopped-child', True)
        self.assertNotIn('unref', [call[0] for call in client.calls])

    def test_direct_or_arbitrary_spec_is_rejected(self):
        client = BufferedClient()
        client.transport = 'direct-private'
        with self.assertRaises(ValueError):
            adapter.SystemdAdapter(client)
        client.transport = 'system-bus'
        spec = model.synthetic_spec(TASK, 'ordinary')
        spec['executable'] = '/bin/other'
        with self.assertRaises(ValueError):
            adapter.SystemdAdapter(client).submit(spec, 10)
        self.assertEqual(client.calls, [])


if __name__ == '__main__':
    unittest.main()
