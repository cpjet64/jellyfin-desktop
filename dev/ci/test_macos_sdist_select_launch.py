import errno
import hashlib
import io
import json
import subprocess
import sys
import unittest
from unittest.mock import Mock, patch

import macos_sdist_select_launch as subject
import macos_sdist_select

subject.selector = macos_sdist_select


class LaunchTests(unittest.TestCase):
    def test_real_isolated_help_imports_sibling_without_archive_read(self):
        flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        static = subject.REPO / "dev/ci/macos_sdist_static.py"
        script = subject.REPO / "dev/ci/macos_sdist_select.py"
        command = [sys.executable, "-I", "-S", "-B", "-c", subject.BOOTSTRAP,
                   str(static), str(script), subject.PINS[static], subject.PINS[script], str(script), "--help"]
        result = subprocess.run(command, capture_output=True, timeout=10, creationflags=flags)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"--source-dir", result.stdout)

    def process(self, output=b"{}", error=b"", code=0):
        process = Mock(pid=123, _handle=321, stdout=io.BytesIO(output), stderr=io.BytesIO(error))
        process.poll.return_value = code
        process.wait.return_value = code
        return process

    def run_mock(self, process, identity=None):
        with patch.object(subject.subprocess, "CREATE_NO_WINDOW", 1, create=True), \
             patch.object(subject.subprocess, "Popen", return_value=process), \
             patch.object(subject, "available", side_effect=lambda s: len(s.getbuffer()) - s.tell()), \
             patch.object(subject, "identity", return_value=identity or {"image": str(subject.PYTHON)}):
            return subject.run_child(["synthetic"])

    def test_normal_child_reaped_and_output_drained(self):
        process = self.process(b"abc", b"error")
        child, code, elapsed, state, failure = self.run_mock(process)
        self.assertIsNone(failure)
        self.assertEqual((code, state["stdout"], state["stderr"]), (0, b"abc", b"error"))
        self.assertEqual(child["image"], str(subject.PYTHON))
        process.wait.assert_called_once()

    def test_identity_failure_kills_and_reaps(self):
        process = self.process(b"early", b"failure")
        process.poll.return_value = None
        with patch.object(subject.subprocess, "CREATE_NO_WINDOW", 1, create=True), \
             patch.object(subject.subprocess, "Popen", return_value=process), \
             patch.object(subject, "available", side_effect=lambda s: len(s.getbuffer()) - s.tell()), \
             patch.object(subject, "identity", side_effect=ValueError("identity")):
            result = subject.run_child(["synthetic"])
            self.assertIn("identity", result[4])
            self.assertEqual((result[3]["stdout"], result[3]["stderr"]), (b"early", b"failure"))
        process.kill.assert_called_once()
        process.wait.assert_called_once()

    def test_cancellation_kills_and_reaps(self):
        process = self.process()
        process.poll.return_value = None
        with patch.object(subject.subprocess, "CREATE_NO_WINDOW", 1, create=True), \
             patch.object(subject.subprocess, "Popen", return_value=process), \
             patch.object(subject, "available", side_effect=lambda s: len(s.getbuffer()) - s.tell()), \
             patch.object(subject, "identity", side_effect=KeyboardInterrupt()):
            self.assertIn("KeyboardInterrupt", subject.run_child(["synthetic"])[4])
        process.kill.assert_called_once()
        process.wait.assert_called_once()

    def test_timeout_kills_and_reaps(self):
        process = self.process()
        process.poll.return_value = None
        with patch.object(subject.subprocess, "CREATE_NO_WINDOW", 1, create=True), \
             patch.object(subject.subprocess, "Popen", return_value=process), \
             patch.object(subject, "available", side_effect=lambda s: len(s.getbuffer()) - s.tell()), \
             patch.object(subject, "identity", return_value={"image": str(subject.PYTHON)}):
            self.assertIn("timeout", subject.run_child(["synthetic"], clock=iter((0, 181, 182)).__next__)[4])
        process.kill.assert_called_once()
        process.wait.assert_called_once()

    def test_output_overflow_fails(self):
        process = self.process(b"abcdef")
        with patch.object(subject, "STDOUT_LIMIT", 3):
            result = self.run_mock(process)
            self.assertIn("output", result[4])
            self.assertEqual(result[3]["stdout"], b"abc")
        process.wait.assert_called()

    def test_launch_failure_is_reported(self):
        with patch.object(subject.subprocess, "CREATE_NO_WINDOW", 1, create=True), \
             patch.object(subject.subprocess, "Popen", side_effect=KeyboardInterrupt()):
            self.assertIn("KeyboardInterrupt", subject.run_child(["synthetic"])[4])

    def test_reader_failure_retains_stable_partial_output(self):
        process = self.process(b"early", b"failure")
        calls = iter((5, 7))
        def failing_available(stream):
            return next(calls, None) if stream.tell() == 0 else (_ for _ in ()).throw(OSError("pipe failure"))
        with patch.object(subject.subprocess, "CREATE_NO_WINDOW", 1, create=True), \
             patch.object(subject.subprocess, "Popen", return_value=process), \
             patch.object(subject, "available", side_effect=failing_available), \
             patch.object(subject, "identity", return_value={"image": str(subject.PYTHON)}):
            result = subject.run_child(["synthetic"])
        self.assertIn("pipe failure", result[4])
        self.assertEqual((result[3]["stdout"], result[3]["stderr"]), (b"early", b"failure"))

    def test_storage_failure_stops_preflight(self):
        with patch.object(subject.os, "name", "nt"), \
             patch.object(subject.OUTPUT.__class__, "exists", return_value=False), \
             patch.object(subject.shutil, "disk_usage", side_effect=OSError(errno.ENOSPC, "full")), \
             patch.object(subject.subprocess, "Popen") as launch:
            with self.assertRaises(OSError):
                subject.preflight()
        launch.assert_not_called()

    def test_validates_records_and_detects_tampering(self):
        records = []
        for archive, spec in subject.selector.ARCHIVES.items():
            for member in spec["members"]:
                records.append({"archive": archive, "archive_bytes": spec["size"],
                                "archive_sha256": spec["sha256"], "member": spec["root"] + "/" + member,
                                "bytes": 1, "text": "x", "sha256": hashlib.sha256(b"x").hexdigest()})
        value = {"expanded": 100, "members": records}
        subject.validate_output(json.dumps(value).encode())
        records[0]["text"] = "y"
        with self.assertRaises(ValueError):
            subject.validate_output(json.dumps(value).encode())


if __name__ == "__main__":
    unittest.main()
