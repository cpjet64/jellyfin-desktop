import os
import json
import ctypes
import re
import subprocess
import sys
import time
import unittest
import uuid

LAUNCHER = os.path.join(os.path.dirname(__file__), 'job_launch.exe')


def child(mode):
    if mode == 'args':
        print(json.dumps(sys.argv[2:]))
        return 0
    if mode == 'exit7':
        return 7
    if mode in ('child', 'grandchild'):
        next_mode = 'grandchild' if mode == 'child' else 'sleep'
        subprocess.Popen([sys.executable, __file__, next_mode], close_fds=True,
                         creationflags=subprocess.CREATE_NO_WINDOW)
        return 0
    if mode in ('natural-child', 'natural-grandchild'):
        if mode == 'natural-child':
            print(os.getpid(), flush=True)
        next_mode = 'natural-grandchild' if mode == 'natural-child' else 'brief'
        subprocess.Popen([sys.executable, __file__, next_mode], close_fds=True,
                         creationflags=subprocess.CREATE_NO_WINDOW)
        if mode == 'natural-child':
            time.sleep(0.15)
        return 0
    if mode == 'brief':
        time.sleep(0.5)
    if mode == 'marker':
        with open(os.path.join(os.path.dirname(__file__), 'assignment-resumed.marker'), 'x'):
            pass
    if mode == 'sleep':
        time.sleep(30)
    return 0


class JobLauncherTests(unittest.TestCase):
    def invoke(self, mode, timeout=5000, env=None):
        receipt = os.path.join(os.path.dirname(__file__), 'receipt-' + uuid.uuid4().hex)
        try:
            result = subprocess.run([LAUNCHER, receipt, str(timeout), os.path.dirname(__file__),
                                     sys.executable, __file__, mode], capture_output=True,
                                    timeout=20, creationflags=subprocess.CREATE_NO_WINDOW, env=env)
            with open(receipt, 'rb') as source:
                result.receipt = source.read()
            return result
        finally:
            if os.path.exists(receipt):
                os.remove(receipt)

    def test_immediate_success(self):
        result = self.invoke('ok')
        self.assertEqual(result.returncode, 0)
        self.assertIn(b'root_exit=0', result.receipt)
        self.assertIn(b'timeout=0 active=0', result.receipt)
        handles = re.search(rb'owned_opened=(\d+) owned_closed=(\d+) close_failed=0', result.receipt)
        self.assertIsNotNone(handles)
        self.assertEqual(handles.group(1), handles.group(2))

    def test_nonzero(self):
        result = self.invoke('exit7')
        self.assertEqual(result.returncode, 7)
        self.assertIn(b'root_exit=7', result.receipt)
        self.assertIn(b'timeout=0 active=0', result.receipt)

    def test_child_and_grandchild_timeout(self):
        result = self.invoke('child', 1000)
        self.assertEqual(result.returncode, 124)
        self.assertIn(b'root_exit=0', result.receipt)
        self.assertIn(b'timeout=1 active=0', result.receipt)

    def test_active_root_timeout(self):
        result = self.invoke('sleep', 500)
        self.assertEqual(result.returncode, 124)
        self.assertIn(b'timeout=1 active=0', result.receipt)

    def test_natural_grandchild_settlement(self):
        start = time.monotonic()
        result = self.invoke('natural-child')
        self.assertEqual(result.returncode, 0)
        self.assertGreaterEqual(time.monotonic() - start, 0.4)
        self.assertIn(b'root_exit=0', result.receipt)
        self.assertIn(b'timeout=0 active=0', result.receipt)

    def test_exited_root_snapshot_does_not_end_job_wait(self):
        receipt = os.path.join(os.path.dirname(__file__), 'receipt-' + uuid.uuid4().hex)
        process = subprocess.Popen([LAUNCHER, receipt, '5000', os.path.dirname(__file__),
                                    sys.executable, __file__, 'natural-child'],
                                   stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            root_pid = int(process.stdout.readline())
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.OpenProcess.restype = ctypes.c_void_p
            handle = kernel.OpenProcess(0x100000, False, root_pid)
            self.assertTrue(handle)
            try:
                self.assertEqual(kernel.WaitForSingleObject(ctypes.c_void_p(handle), 4000), 0)
            finally:
                kernel.CloseHandle(ctypes.c_void_p(handle))
            self.assertIsNone(process.poll())
            self.assertEqual(process.wait(timeout=10), 0)
            with open(receipt, 'rb') as source:
                self.assertIn(b'timeout=0 active=0', source.read())
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=10)
            process.stdout.close()
            if os.path.exists(receipt):
                os.remove(receipt)

    def test_assignment_failure_does_not_resume(self):
        marker = os.path.join(os.path.dirname(__file__), 'assignment-resumed.marker')
        self.assertFalse(os.path.exists(marker))
        env = os.environ.copy()
        env['W1214_TEST_ASSIGN_FAIL'] = '1'
        result = self.invoke('marker', env=env)
        self.assertEqual(result.returncode, 125)
        self.assertIn(b'assign_failed=1 terminated=1 settled=1', result.receipt)
        self.assertFalse(os.path.exists(marker))

    def test_early_close_never_passes(self):
        env = os.environ.copy()
        env['W1214_TEST_EARLY_CLOSE'] = '1'
        result = self.invoke('sleep', env=env)
        self.assertEqual(result.returncode, 125)
        self.assertIn(b'job_proof=unavailable', result.receipt)
        self.assertNotIn(b'timeout=0 active=0', result.receipt)

    def test_unrelated_process_survives_job_timeout(self):
        unrelated = subprocess.Popen([sys.executable, __file__, 'sleep'],
                                     creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            result = self.invoke('sleep', 500)
            self.assertEqual(result.returncode, 124)
            self.assertIsNone(unrelated.poll())
        finally:
            unrelated.terminate()
            unrelated.wait(timeout=10)

    def test_arguments_round_trip(self):
        receipt = os.path.join(os.path.dirname(__file__), 'receipt-' + uuid.uuid4().hex)
        expected = ['has spaces', 'a"b', 'trailing\\', '']
        try:
            result = subprocess.run([LAUNCHER, receipt, '5000', os.path.dirname(__file__),
                                     sys.executable, __file__, 'args'] + expected,
                                    capture_output=True, timeout=20,
                                    creationflags=subprocess.CREATE_NO_WINDOW)
            self.assertEqual(result.returncode, 0)
            self.assertEqual(json.loads(result.stdout), expected)
        finally:
            if os.path.exists(receipt):
                os.remove(receipt)

    def test_existing_receipt_prevents_launch(self):
        receipt = os.path.join(os.path.dirname(__file__), 'receipt-' + uuid.uuid4().hex)
        marker = os.path.join(os.path.dirname(__file__), 'assignment-resumed.marker')
        self.assertFalse(os.path.exists(marker))
        try:
            with open(receipt, 'xb') as output:
                output.write(b'unchanged')
            result = subprocess.run([LAUNCHER, receipt, '5000', os.path.dirname(__file__),
                                     sys.executable, __file__, 'marker'], capture_output=True,
                                    timeout=20, creationflags=subprocess.CREATE_NO_WINDOW)
            self.assertEqual(result.returncode, 125)
            self.assertFalse(os.path.exists(marker))
            with open(receipt, 'rb') as source:
                self.assertEqual(source.read(), b'unchanged')
        finally:
            if os.path.exists(receipt):
                os.remove(receipt)


if __name__ == '__main__':
    if len(sys.argv) > 1:
        raise SystemExit(child(sys.argv[1]))
    unittest.main()
