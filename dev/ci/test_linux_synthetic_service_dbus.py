"""Tests against a fake dbus-next-shaped bus; never opens a bus."""
import asyncio
from types import SimpleNamespace
import unittest

import linux_synthetic_service as model
import linux_synthetic_service_adapter as adapter
import linux_synthetic_service_dbus as binding

TASK = 'a' * 32
START = '/org/freedesktop/systemd1/job/42'
STOP = '/org/freedesktop/systemd1/job/43'


class Message:
    def __init__(self, **kwargs):
        self.__dict__.update(kwargs)


class Variant:
    def __init__(self, signature, value):
        self.signature, self.value = signature, value


class Types:
    SIGNAL = 'signal'
    ERROR = 'error'
    METHOD_RETURN = 'reply'


class FakeBus:
    connected = True
    unique_name = ':1.50'

    def __init__(self):
        self.calls, self.handlers = [], []
        self.unit = None
        self.stopped = False
        self.owner = ':1.2'
        self.disconnect_on = None
        self.change_owner_on = None

    def add_message_handler(self, handler):
        self.handlers.append(handler)

    def emit_job(self, job):
        message = Message(message_type=Types.SIGNAL, sender=':1.2', interface=binding.MANAGER,
                          member='JobRemoved', path=binding.PATH, signature='uoss',
                          body=[int(job.rsplit('/', 1)[1]), job, self.unit, 'done'])
        for handler in self.handlers:
            handler(message)

    async def call(self, message):
        self.calls.append(message)
        member = message.member
        body = []
        if member == 'GetNameOwner':
            body = [self.owner]
        elif member == 'StartTransientUnit':
            self.unit = message.body[0]
            self.emit_job(START)
            body = [START]
        elif member == 'StopUnit':
            self.stopped = True
            self.emit_job(STOP)
            body = [STOP]
        elif member == 'GetUnit':
            body = ['/org/freedesktop/systemd1/unit/fake']
        elif member == 'GetAll':
            if message.body[0].endswith('.Unit'):
                body = [{'InvocationID': Variant('ay', bytes.fromhex('bb' * 16))}]
            else:
                body = [{'ControlGroup': Variant('s', '/system.slice/' + self.unit)}]
        if member == self.disconnect_on:
            self.connected = False
        if member == self.change_owner_on:
            self.owner = ':1.3'
        return SimpleNamespace(message_type='reply', signature=binding.REPLIES[member], body=body)


class Observer:
    def __init__(self, bus):
        self.bus, self.sequence, self.captures = bus, 0, 0

    def watch_name(self, unit, deadline):
        return True

    def cursor(self, unit, deadline):
        return self.sequence

    def capture(self, unit, cursor, deadline):
        self.captures += 1
        if self.captures == 1:
            self.sequence += 1
        active = not self.bus.stopped
        return {'unit': unit, 'job': START, 'invocation': 'b' * 32, 'cgroup': '/system.slice/' + unit,
                'watch_cursor': cursor, 'provenance': 'independent-observer', 'observation_revision': self.captures,
                'reference_held': self.bus.connected, 'state': 'active' if active else 'inactive',
                'job_terminal': True, 'cgroup_empty': not active, 'descendants_absent': not active,
                'evidence_saved': True, 'fixture_ready': self.captures > 1,
                'fixture_mode': 'stopped-child', 'fixture_result': None if active else 'passed'}

    def next_change(self, unit, cursor, deadline):
        if self.sequence <= cursor:
            raise TimeoutError('no change')
        return self.sequence


class DbusBindingTest(unittest.TestCase):
    def setUp(self):
        self.loop = asyncio.new_event_loop()
        self.addCleanup(self.loop.close)
        self.bus = FakeBus()
        self.bus._loop = self.loop
        self.observer = Observer(self.bus)
        self.client = binding.DbusNextClient(self.bus, self.loop, Message, Variant, Types,
                                             self.observer, system_bus_verified=True, clock=lambda: 100)

    def test_actual_messagebus_interface_buffers_early_jobs_and_maps_types(self):
        result = model.SyntheticService(adapter.SystemdAdapter(self.client), clock=lambda: 100).run(TASK, 'stopped-child', True)
        self.assertEqual(result['fixture_result'], 'passed')
        calls = self.bus.calls
        members = [message.member for message in calls]
        self.assertLess(members.index('AddMatch'), members.index('Subscribe'))
        self.assertLess(members.index('Subscribe'), members.index('StartTransientUnit'))
        start = next(message for message in calls if message.member == 'StartTransientUnit')
        self.assertEqual(start.signature, 'ssa(sv)a(sa(sv))')
        properties = dict(start.body[2])
        self.assertEqual((properties['AddRef'].signature, properties['AddRef'].value), ('b', True))
        self.assertEqual((properties['TimeoutStopUSec'].signature, properties['TimeoutStopUSec'].value), ('t', 8_000_000))
        self.assertEqual(properties['ExecStart'].signature, 'a(sasb)')
        self.assertIn('UnrefUnit', members)
        self.assertTrue(all(message.destination == ':1.2' for message in calls
                            if message.interface in (binding.MANAGER, binding.PROPERTIES)))

    def test_disconnected_or_overflowed_watch_never_unref(self):
        self.client.overflow = True
        self.client.owner = ':1.2'
        with self.assertRaises(model.Unconfirmed):
            self.client.wait_job(0, START, 110)
        self.assertFalse(any(message.member == 'UnrefUnit' for message in self.bus.calls))
        self.bus.connected = False
        with self.assertRaises(model.Unconfirmed):
            self.client.job_cursor(110)

    def test_async_call_timeout_does_not_disconnect_or_unref(self):
        async def blocked(_message):
            await asyncio.Event().wait()
        self.bus.call = blocked
        self.client.clock = lambda: 100
        with self.assertRaises(TimeoutError):
            self.client._call(binding.DBUS, binding.DBUS_PATH, binding.DBUS, 'GetNameOwner', 's',
                              [binding.DEST], 100.001)
        self.assertTrue(self.bus.connected)
        self.assertFalse(any(message.member == 'UnrefUnit' for message in self.bus.calls))

    def test_post_reply_disconnect_retains_returned_job_and_never_unref(self):
        for member, expected_field, expected in (('StartTransientUnit', 'submitted_job', START),
                                                  ('StopUnit', 'stop_job', STOP)):
            with self.subTest(member=member):
                bus = FakeBus()
                bus._loop = self.loop
                bus.disconnect_on = member
                observer = Observer(bus)
                client = binding.DbusNextClient(bus, self.loop, Message, Variant, Types, observer,
                                                system_bus_verified=True, clock=lambda: 100)
                runner = model.SyntheticService(adapter.SystemdAdapter(client), clock=lambda: 100)
                with self.assertRaises(model.JobReplyUnconfirmed) as raised:
                    runner.run(TASK, 'stopped-child', True)
                self.assertEqual(raised.exception.last_known[expected_field], expected)
                self.assertFalse(any(message.member == 'UnrefUnit' for message in bus.calls))

    def test_manager_owner_change_during_evidence_is_unconfirmed(self):
        original = self.observer.capture
        def capture(unit, cursor, deadline):
            result = original(unit, cursor, deadline)
            self.bus.owner = ':1.3'
            return result
        self.observer.capture = capture
        with self.assertRaises(model.Unconfirmed):
            model.SyntheticService(adapter.SystemdAdapter(self.client), clock=lambda: 100).run(TASK, 'stopped-child', True)
        self.assertFalse(any(message.member in ('StopUnit', 'UnrefUnit') for message in self.bus.calls))

    def test_owner_change_after_stop_reply_retains_job(self):
        self.bus.change_owner_on = 'StopUnit'
        runner = model.SyntheticService(adapter.SystemdAdapter(self.client), clock=lambda: 100)
        with self.assertRaises(model.Unconfirmed) as raised:
            runner.run(TASK, 'stopped-child', True)
        self.assertEqual(raised.exception.last_known['stop_job'], STOP)
        self.assertFalse(any(message.member == 'UnrefUnit' for message in self.bus.calls))

    def test_stop_wrong_signature_retains_valid_job(self):
        original = self.bus.call
        async def wrong_signature(message):
            reply = await original(message)
            if message.member == 'StopUnit':
                reply.signature = 's'
            return reply
        self.bus.call = wrong_signature
        runner = model.SyntheticService(adapter.SystemdAdapter(self.client), clock=lambda: 100)
        with self.assertRaises(model.JobReplyUnconfirmed) as raised:
            runner.run(TASK, 'stopped-child', True)
        self.assertEqual(raised.exception.last_known['stop_job'], STOP)
        self.assertEqual(sum(message.member == 'StopUnit' for message in self.bus.calls), 1)
        self.assertFalse(any(message.member == 'UnrefUnit' for message in self.bus.calls))

    def test_owner_change_after_unref_is_unconfirmed(self):
        self.bus.change_owner_on = 'UnrefUnit'
        runner = model.SyntheticService(adapter.SystemdAdapter(self.client), clock=lambda: 100)
        with self.assertRaisesRegex(model.Unconfirmed, 'reference release unconfirmed'):
            runner.run(TASK, 'stopped-child', True)
        self.assertTrue(self.client.reference_held)

    def test_unrelated_signals_do_not_fill_bounded_task_watch(self):
        self.client.owner = ':1.2'
        self.client.watched_unit = 'w644-' + TASK + '.service'
        self.bus.add_message_handler(self.client._signal)
        self.bus.unit = 'unrelated.service'
        for _ in range(binding.MAX_JOBS + 5):
            self.bus.emit_job(START)
        self.assertEqual(self.client.jobs, [])
        self.bus.unit = self.client.watched_unit
        for _ in range(binding.MAX_JOBS + 1):
            self.bus.emit_job(START)
        self.assertTrue(self.client.overflow)
        self.assertEqual(len(self.client.jobs), binding.MAX_JOBS)

    def test_running_or_foreign_loop_is_rejected(self):
        async def nested():
            with self.assertRaises(model.Unconfirmed):
                self.client._connected()
        self.loop.run_until_complete(nested())
        other = asyncio.new_event_loop()
        try:
            with self.assertRaises(ValueError):
                binding.DbusNextClient(self.bus, other, Message, Variant, Types, self.observer, system_bus_verified=True)
        finally:
            other.close()

    def test_unverified_system_bus_is_rejected(self):
        with self.assertRaises(ValueError):
            binding.DbusNextClient(self.bus, self.loop, Message, Variant, Types, self.observer,
                                   system_bus_verified=False)


if __name__ == '__main__':
    unittest.main()
