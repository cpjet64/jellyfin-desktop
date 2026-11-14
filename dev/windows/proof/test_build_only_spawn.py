"""Portable mocked coverage of the fixed fixture and production launch paths."""
import ctypes
import os
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from build_only import fixture_child, spawn, supervisor, winapi


class FakeKernel:
    def __init__(self):
        self.events = []
        self.duplicates = 0

    def GetCurrentProcess(self):
        return 99

    def CreateFileW(self, name, access, sharing, attributes, disposition, flags, template):
        self.events.append(('stdin', name, bool(ctypes.cast(attributes, ctypes.POINTER(winapi.SECURITY_ATTRIBUTES)).contents.inherit)))
        return 11

    def DuplicateHandle(self, source_process, source, target_process, output, access, inherit, options):
        self.duplicates += 1
        value = 20 + self.duplicates
        ctypes.cast(output, ctypes.POINTER(winapi.HANDLE))[0] = value
        self.events.append(('duplicate', source, value, bool(inherit)))
        return 1

    def InitializeProcThreadAttributeList(self, storage, count, flags, size):
        ctypes.cast(size, ctypes.POINTER(winapi.SIZE_T))[0] = 64
        return int(storage is not None)

    def UpdateProcThreadAttribute(self, storage, flags, attribute, value, size, previous, returned):
        handles = ctypes.cast(value, ctypes.POINTER(winapi.HANDLE))
        self.events.append(('allowlist', attribute, tuple(handles[index] for index in range(3)), size))
        return 1

    def DeleteProcThreadAttributeList(self, storage):
        self.events.append(('attributes_closed',))

    def CreateProcessW(self, executable, command, process_attrs, thread_attrs, inherit, flags, environment,
                       cwd, startup, info):
        details = ctypes.cast(startup, ctypes.POINTER(winapi.STARTUPINFO)).contents
        self.events.append(('create', executable, command.value, bool(inherit), flags, cwd,
                            details.flags, (details.stdin, details.stdout, details.stderr), environment is not None))
        result = ctypes.cast(info, ctypes.POINTER(winapi.PROCESS_INFORMATION)).contents
        result.process, result.thread, result.pid, result.tid = 101, 102, 103, 104
        return 1

    def AssignProcessToJobObject(self, job, process):
        self.events.append(('assign', job, process))
        return 1

    def ResumeThread(self, thread):
        self.events.append(('resume', thread))
        return 1

    def CloseHandle(self, handle):
        self.events.append(('close', handle))
        return 1


class FakeAPI:
    def __init__(self):
        self.k = FakeKernel()
        self.opened = self.closed = self.close_failed = 0

    def check(self, value):
        if not value:
            raise AssertionError('unexpected native failure')
        return value


def fixture_policy(mode):
    directory = Path(supervisor.__file__).absolute().parent
    target = directory.parents[3] / 'deps' / 'fixture-case'
    executable = str(Path(sys.executable))
    child = str(directory / 'fixture_child.py')
    output = str(target / 'result.bin')
    command = SimpleNamespace(executable=executable, executable_sha256='a', executable_size=1,
                              response_path=child, args=('-B', child, mode, output), outputs=(output,),
                              cwd=str(target), stdout=str(target / 'stdout.log'), stderr=str(target / 'stderr.log'))
    return SimpleNamespace(kind='fixture', command_form='fixture', deadline_ms=4000, grace_ms=100,
                           telemetry_version='fixture-python', telemetry_path=executable,
                           telemetry_sha256='a', telemetry_size=1, command=command)


class LaunchSelectionTests(unittest.TestCase):
    def test_only_validated_fixture_modes_select_detached_launch(self):
        with patch.object(Path, 'resolve', lambda path, **kwargs: path), patch.object(Path, 'is_dir', return_value=True):
            for mode in ('natural', 'residual', 'rootfail'):
                with self.subTest(mode=mode):
                    self.assertIs(supervisor._select_launcher(fixture_policy(mode)), spawn._launch_fixture)
            self.assertIs(supervisor._select_launcher(fixture_policy('timeout')), spawn.launch)
            invalid = fixture_policy('residual')
            invalid.command.response_path += '.other'
            with self.assertRaises(ValueError):
                supervisor._select_launcher(invalid)

    def test_production_selects_existing_launch_and_registry_stays_empty(self):
        from build_only.policy import PRODUCTION_POLICIES
        self.assertEqual(PRODUCTION_POLICIES, {})
        self.assertIs(supervisor._select_launcher(SimpleNamespace(kind='production')), spawn.launch)

    def test_command_inspection_precedes_launch_selection(self):
        with patch.dict(sys.modules, {'msvcrt': SimpleNamespace()}), \
             patch.object(supervisor, 'inspect_command_inputs', side_effect=ValueError('reject')), \
             patch.object(supervisor, '_select_launcher') as select:
            with self.assertRaisesRegex(ValueError, 'reject'):
                supervisor._run_job(None, object(), {}, [], {}, object())
        select.assert_not_called()

    def assert_launch(self, fixture, environment):
        api = FakeAPI()
        job = SimpleNamespace(value=77)
        launch = spawn._launch_fixture if fixture else spawn.launch
        def verify(process, owned_job):
            api.k.events.append(('verify', process.value, owned_job.value))
        with patch.object(spawn.os, 'name', 'nt'), patch.object(spawn.os.path, 'isabs', return_value=True):
            root = launch(api, job, 'fixture.exe', ['-B', 'fixture.py', 'natural', 'result.bin'],
                          'work', 31, 32, verify, environment)
        events = api.k.events
        creation = next(event for event in events if event[0] == 'create')
        expected = (winapi.CREATE_SUSPENDED | winapi.EXTENDED_STARTUPINFO_PRESENT |
                    (winapi.DETACHED_PROCESS if fixture else winapi.CREATE_NO_WINDOW))
        if environment is not None:
            expected |= spawn.CREATE_UNICODE_ENVIRONMENT
        self.assertEqual(creation[4], expected)
        self.assertEqual(creation[6], winapi.STARTF_USESTDHANDLES)
        self.assertEqual(creation[7], (11, 21, 22))
        self.assertTrue(creation[3])
        self.assertEqual(creation[8], environment is not None)
        self.assertEqual(creation[2], subprocess.list2cmdline(['fixture.exe', '-B', 'fixture.py', 'natural', 'result.bin']))
        self.assertIn(('stdin', 'NUL', True), events)
        self.assertIn(('allowlist', winapi.PROC_THREAD_ATTRIBUTE_HANDLE_LIST, (11, 21, 22),
                       ctypes.sizeof(winapi.HANDLE) * 3), events)
        sequence = [event[0] for event in events]
        self.assertLess(sequence.index('create'), sequence.index('assign'))
        self.assertLess(sequence.index('assign'), sequence.index('verify'))
        self.assertLess(sequence.index('verify'), sequence.index('resume'))
        root.close()
        self.assertEqual((api.opened, api.closed, api.close_failed), (5, 5, 0))

    def test_fixture_launch_uses_detached_and_allowlisted_handles(self):
        self.assert_launch(True, {'KEY': 'value'})

    def test_production_launch_flags_remain_unchanged(self):
        self.assert_launch(False, None)

    def test_failed_root_verification_never_resumes(self):
        api, job = FakeAPI(), SimpleNamespace(value=77)
        def reject(process, owned_job):
            api.k.events.append(('verify', process.value, owned_job.value))
            raise ValueError('root mismatch')
        with patch.object(spawn.os, 'name', 'nt'), patch.object(spawn.os.path, 'isabs', return_value=True), \
             patch.object(spawn, '_emergency_settle', return_value=(None, False)) as cleanup:
            with self.assertRaises(spawn.LaunchFailure):
                spawn._launch_fixture(api, job, 'fixture.exe', [], 'work', 31, 32, reject)
        self.assertEqual(cleanup.call_count, 1)
        events = [event[0] for event in api.k.events]
        self.assertLess(events.index('assign'), events.index('verify'))
        self.assertNotIn('resume', events)

    def test_fixture_child_exact_detached_vector_and_null_handles(self):
        with patch.object(fixture_child.subprocess, 'Popen') as popen:
            fixture_child.spawn('timeout', 'result.bin')
        popen.assert_called_once_with([sys.executable, '-B', fixture_child.__file__, 'timeout', 'result.bin'],
                                      close_fds=True, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                      stderr=subprocess.DEVNULL, creationflags=0x00000008)


if __name__ == '__main__':
    unittest.main()
