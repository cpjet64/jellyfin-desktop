"""Narrow dbus-next 0.2.3 MessageBus binding; no connection is opened here.

The caller injects an already connected system MessageBus, its asyncio loop,
dbus_next.Message, dbus_next.Variant, MessageType, and a separately qualified
fixture evidence watcher. Retain this owner and its connection on uncertainty.
"""
import asyncio
import threading
import time

import linux_synthetic_service as model

DEST = 'org.freedesktop.systemd1'
PATH = '/org/freedesktop/systemd1'
MANAGER = 'org.freedesktop.systemd1.Manager'
DBUS = 'org.freedesktop.DBus'
DBUS_PATH = '/org/freedesktop/DBus'
PROPERTIES = 'org.freedesktop.DBus.Properties'
MAX_JOBS = 1024


class DbusNextClient:
    transport = 'system-bus'

    def __init__(self, bus, loop, Message, Variant, MessageType, observer, *, system_bus_verified, clock=time.monotonic):
        if (system_bus_verified is not True or not bus.connected or not bus.unique_name or
                getattr(bus, '_loop', None) is not loop or loop.is_running()):
            raise ValueError('already connected system bus required')
        self.bus, self.loop, self.Message, self.Variant, self.MessageType = bus, loop, Message, Variant, MessageType
        self.observer, self.clock, self.unique_name = observer, clock, bus.unique_name
        self.owner = None
        self.thread_id = threading.get_ident()
        self.jobs = []
        self.changed = asyncio.Event()
        self.overflow = False
        self.reference_held = False
        self.watched_unit = None

    def _connected(self):
        try:
            asyncio.get_running_loop()
        except RuntimeError:
            running = False
        else:
            running = True
        if (not self.bus.connected or self.bus.unique_name != self.unique_name or
                getattr(self.bus, '_loop', None) is not self.loop or self.loop.is_running() or running or
                threading.get_ident() != self.thread_id):
            raise model.Unconfirmed('system bus connection changed')

    def _call(self, destination, path, interface, member, signature, body, deadline, job_reply=False):
        self._connected()
        timeout = model.remaining(self.clock, deadline)
        message = self.Message(destination=destination, path=path, interface=interface,
                               member=member, signature=signature, body=body)
        reply = self.loop.run_until_complete(asyncio.wait_for(self.bus.call(message), timeout))
        if reply is None or reply.message_type != self.MessageType.METHOD_RETURN:
            raise model.Unconfirmed('bus call failed or reply absent')
        try:
            self._connected()
        except model.Unconfirmed as error:
            if job_reply and len(reply.body) == 1:
                raise model.JobReplyUnconfirmed(reply.body[0]) from error
            raise
        return reply.body

    def _assert_owner(self, deadline):
        owner = self._call(DBUS, DBUS_PATH, DBUS, 'GetNameOwner', 's', [DEST], deadline)
        if len(owner) != 1 or owner[0] != self.owner:
            raise model.Unconfirmed('systemd bus owner changed')

    def _manager(self, member, signature, body, deadline, job_reply=False):
        self._assert_owner(deadline)
        return self._call(self.owner, PATH, MANAGER, member, signature, body, deadline, job_reply=job_reply)

    def _signal(self, message):
        if (message.message_type != self.MessageType.SIGNAL or message.sender != self.owner or
                message.interface != MANAGER or message.member != 'JobRemoved' or message.path != PATH):
            return
        row = message.body
        if (len(row) != 4 or not isinstance(row[0], int) or isinstance(row[0], bool) or
                not isinstance(row[1], str) or not isinstance(row[2], str) or not isinstance(row[3], str) or
                row[2] != self.watched_unit or row[1] != '/org/freedesktop/systemd1/job/' + str(row[0]) or
                row[3] not in ('done', 'canceled', 'timeout', 'failed', 'dependency', 'skipped')):
            return
        if len(self.jobs) >= MAX_JOBS:
            self.overflow = True
        else:
            self.jobs.append(tuple(row))
        self.changed.set()

    def subscribe(self, deadline):
        owner = self._call(DBUS, DBUS_PATH, DBUS, 'GetNameOwner', 's', [DEST], deadline)
        if len(owner) != 1 or not isinstance(owner[0], str) or not owner[0].startswith(':'):
            raise model.Unconfirmed('systemd bus owner unavailable')
        self.owner = owner[0]
        self.bus.add_message_handler(self._signal)
        rule = "type='signal',sender='org.freedesktop.systemd1',interface='org.freedesktop.systemd1.Manager',member='JobRemoved'"
        self._call(DBUS, DBUS_PATH, DBUS, 'AddMatch', 's', [rule], deadline)
        self._manager('Subscribe', '', [], deadline)
        return True

    def watch_jobs(self, deadline):
        model.remaining(self.clock, deadline)
        return len(self.jobs)

    def watch_unit(self, unit, deadline):
        model.remaining(self.clock, deadline)
        if self.observer.watch_name(unit, deadline) is not True:
            return False
        self.watched_unit = unit
        return True

    def start_transient(self, unit, mode, properties, auxiliary, deadline):
        values = [[name, self.Variant(signature, [list(item) for item in value] if signature == 'a(sasb)' else value)]
                  for name, signature, value in properties]
        reply = self._manager('StartTransientUnit', 'ssa(sv)a(sa(sv))', [unit, mode, values, auxiliary], deadline,
                              job_reply=True)
        if len(reply) != 1:
            raise model.Unconfirmed('start reply missing job')
        self.reference_held = True
        return reply[0]

    def wait_job(self, cursor, job, deadline):
        while True:
            self._assert_owner(deadline)
            if self.overflow:
                raise model.Unconfirmed('job watch overflow')
            self.changed.clear()
            for row in self.jobs[cursor:]:
                if len(row) == 4 and row[1] == job:
                    self._assert_owner(deadline)
                    return {'job': row[1], 'unit': row[2], 'result': row[3], 'terminal': True}
            timeout = model.remaining(self.clock, deadline)
            self.loop.run_until_complete(asyncio.wait_for(self.changed.wait(), timeout))

    def unit_identity(self, unit, deadline):
        path = self._manager('GetUnit', 's', [unit], deadline)[0]
        unit_values = self._call(self.owner, path, PROPERTIES, 'GetAll', 's', ['org.freedesktop.systemd1.Unit'], deadline)[0]
        service = self._call(self.owner, path, PROPERTIES, 'GetAll', 's', ['org.freedesktop.systemd1.Service'], deadline)[0]
        self._assert_owner(deadline)
        return {'invocation': bytes(unit_values['InvocationID'].value).hex(),
                'cgroup': str(service['ControlGroup'].value), 'reference_held': self.reference_held}

    def unit_cursor(self, unit, deadline):
        self._assert_owner(deadline)
        value = self.observer.cursor(unit, deadline)
        self._assert_owner(deadline)
        return value

    def capture_evidence(self, identity, deadline):
        self._assert_owner(deadline)
        value = self.observer.capture(identity, deadline)
        self._assert_owner(deadline)
        return value

    def wait_unit_change(self, unit, cursor, deadline):
        self._assert_owner(deadline)
        value = self.observer.next_change(unit, cursor, deadline)
        self._assert_owner(deadline)
        return value

    def job_cursor(self, deadline):
        self._assert_owner(deadline)
        model.remaining(self.clock, deadline)
        return len(self.jobs)

    def stop_unit(self, unit, mode, deadline):
        reply = self._manager('StopUnit', 'ss', [unit, mode], deadline, job_reply=True)
        if len(reply) != 1:
            raise model.Unconfirmed('stop reply missing job')
        return reply[0]

    def unref_unit(self, unit, deadline):
        self._manager('UnrefUnit', 's', [unit], deadline)
        self._assert_owner(deadline)
        self.reference_held = False
