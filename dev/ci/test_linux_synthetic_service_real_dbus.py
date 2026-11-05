"""Required offline compatibility check; the pinned wheel is never installed."""
import asyncio
import hashlib
import os
from pathlib import Path
import socket
import sys
import tempfile
import unittest
from unittest import mock

import linux_synthetic_service_adapter as adapter
import linux_synthetic_service_dbus as binding
import linux_synthetic_service as model
import provision_dbus_next as provisioner
from test_linux_synthetic_service_dbus import FakeBus, Observer, TASK


def verified_wheel():
    path = Path(os.environ.get('W693_DBUS_NEXT_WHEEL', provisioner.WHEEL))
    data = provisioner.read_bounded(path)
    if len(data) != 57885 or hashlib.sha256(data).hexdigest() != '58948f9aff9db08316734c0be2a120f6dc502124d9642f55e90ac82ffb16a18b':
        raise RuntimeError('dbus-next wheel size or hash mismatch')
    return str(path)


class RealMessageTest(unittest.TestCase):
    def test_download_requires_bounded_curl_version(self):
        calls = []
        def run(args, **kwargs):
            calls.append((args, kwargs))
            return mock.Mock(stdout=b'curl 8.4.0 example' if len(calls) == 1 else b'wheel')
        self.assertEqual(provisioner.download(run), b'wheel')
        self.assertEqual(calls[0][0][1], '--disable')
        self.assertEqual(calls[1][0][1], '--disable')
        self.assertIn('--max-filesize', calls[1][0])
        self.assertNotIn('--compressed', calls[1][0])
        self.assertEqual(calls[1][1]['timeout'], 60)
        for version in (b'curl 8.3.0', b'unknown'):
            with self.assertRaisesRegex(RuntimeError, '8.4.0'):
                provisioner.download(lambda *args, **kwargs: mock.Mock(stdout=version))

    def test_provision_uses_injected_transport_and_refuses_bad_cache(self):
        data = Path(verified_wheel()).read_bytes()
        local_deps = provisioner.ROOT / 'deps'
        local_deps.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=local_deps) as directory:
            root = Path(directory).resolve()
            wheel = root / 'deps' / 'w693-dbus-next' / 'dbus_next-0.2.3-py3-none-any.whl'
            (root / 'deps').mkdir()
            def fake_fetch():
                return data
            with mock.patch.object(provisioner, 'ROOT', root):
                with mock.patch.object(Path, 'is_symlink', return_value=True):
                    with self.assertRaisesRegex(RuntimeError, 'reparse'):
                        provisioner.provision(wheel, fake_fetch)
                with self.assertRaisesRegex(RuntimeError, 'digest'):
                    provisioner.provision(wheel, lambda: b'bad')
                self.assertFalse(wheel.exists())
                provisioner.provision(wheel, fake_fetch)
                self.assertEqual(wheel.read_bytes(), data)
                wheel.write_bytes(b'bad')
                with self.assertRaises(RuntimeError):
                    provisioner.provision(wheel, fake_fetch)
                self.assertEqual(wheel.read_bytes(), b'bad')

    def test_real_types_and_marshalling_against_fake_transport(self):
        wheel = verified_wheel()
        sys.path.insert(0, wheel)
        self.addCleanup(sys.path.remove, wheel)
        loop = asyncio.new_event_loop()
        self.addCleanup(loop.close)
        with mock.patch.object(socket.socket, 'connect', side_effect=AssertionError('network forbidden')), \
                mock.patch.object(socket.socket, 'connect_ex', side_effect=AssertionError('network forbidden')), \
                mock.patch.object(socket.socket, 'sendto', side_effect=AssertionError('network forbidden')), \
                mock.patch.object(socket, 'create_connection', side_effect=AssertionError('network forbidden')):
            from dbus_next import Message, MessageType, Variant
            bus = FakeBus()
            bus._loop = loop
            async def reply(message):
                message.serial = 1
                message._marshall()
                if message.member == 'GetNameOwner':
                    return Message.new_method_return(message, 's', [bus.owner])
                if message.member == 'StartTransientUnit':
                    return Message.new_method_return(message, 'o', ['/org/freedesktop/systemd1/job/42'])
                if message.member == 'StopUnit':
                    return Message.new_method_return(message, 'o', ['/org/freedesktop/systemd1/job/43'])
                if message.member == 'GetUnit':
                    return Message.new_method_return(message, 'o', ['/org/freedesktop/systemd1/unit/fake'])
                if message.member == 'GetAll':
                    values = ({'InvocationID': Variant('ay', bytes.fromhex('bb' * 16))}
                              if message.body[0].endswith('.Unit') else
                              {'ControlGroup': Variant('s', '/system.slice/' + unit)})
                    return Message.new_method_return(message, 'a{sv}', [values])
                return Message.new_method_return(message)
            bus.call = reply
            client = binding.DbusNextClient(bus, loop, Message, Variant, MessageType, Observer(bus),
                                            system_bus_verified=True, clock=lambda: 100)
            client.subscribe(110)
            spec = model.synthetic_spec(TASK, 'ordinary')
            unit = spec['unit']
            job = client.start_transient(spec['unit'], 'fail', adapter.typed_properties(spec), [], 110)
            self.assertEqual(job, '/org/freedesktop/systemd1/job/42')
            self.assertEqual(client.unit_identity(unit, 110)['invocation'], 'b' * 32)
            self.assertEqual(client.stop_unit(unit, 'fail', 110), '/org/freedesktop/systemd1/job/43')
            client.unref_unit(unit, 110)
            signal = Message.new_signal(binding.PATH, binding.MANAGER, 'JobRemoved', 'uoss',
                                        [42, job, unit, 'done'])
            signal.sender = ':1.2'
            signal._marshall()
            client.watched_unit = unit
            client._signal(signal)
            self.assertEqual(client.jobs[0][1], job)
            async def wrong_signature(message):
                message.serial = 1
                return Message.new_method_return(message, 's', ['wrong'])
            bus.call = wrong_signature
            with self.assertRaises(model.Unconfirmed):
                client._call(':1.2', binding.PATH, binding.MANAGER, 'StopUnit', 'ss', [unit, 'fail'], 110)
            async def error_reply(message):
                message.serial = 1
                return Message.new_error(message, 'org.example.Error', 'rejected')
            bus.call = error_reply
            with self.assertRaises(model.Unconfirmed):
                client._call(':1.2', binding.PATH, binding.MANAGER, 'StopUnit', 'ss', [unit, 'fail'], 110)


if __name__ == '__main__':
    unittest.main()
