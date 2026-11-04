import ast
import builtins
import errno
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import macos_sdist_supervise as subject


def setUpModule():
    (subject.REPO / "deps").mkdir(exist_ok=True)


class SyntheticGroup(Exception):
    def __init__(self, message, exceptions):
        super().__init__(message)
        self.exceptions = exceptions


class FakeProcess:
    pid = 1234

    def __init__(self, running):
        self.running = running
        self.killed = False

    def poll(self):
        return None if self.running else 0

    def kill(self):
        self.killed = True
        self.running = False

    def wait(self, timeout):
        self.running = False
        return 1 if self.killed else 0


class SupervisorTests(unittest.TestCase):
    def invoke_main(self, base, result, validation=None):
        argv = ["supervisor", "--worker-sha256", "a" * 64,
                "--manifest-sha256", "b" * 64, "--python-sha256", "c" * 64]
        with patch.object(subject.sys, "argv", argv), patch.object(subject, "prepare", return_value=base), \
                patch.object(subject, "run_worker", return_value=result), \
                patch.object(subject, "validate_result", side_effect=validation):
            return subject.main()

    def worker_outcome(self, **overrides):
        result = {"child_pid": 1234, "exit_code": 0, "forced_stop": False, "child_reaped": True,
                  "output_within_limit": True, "elapsed_seconds": 1.5, "stdout_bytes": 3, "stderr_bytes": 4}
        result.update(overrides)
        return result

    def test_ordinary_worker_failure_retains_outcome(self):
        with tempfile.TemporaryDirectory(dir=subject.REPO / "deps") as directory:
            base = Path(directory)
            result = self.worker_outcome(exit_code=7)
            self.assertEqual(self.invoke_main(base, result), 7)
            outcome = json.loads((base / "worker-outcome.json").read_text(encoding="utf-8"))
            self.assertEqual(outcome, {"status": "failed", **result})
            self.assertFalse((base / "supervisor-receipt.json").exists())

    def test_timeout_retains_outcome(self):
        with tempfile.TemporaryDirectory(dir=subject.REPO / "deps") as directory:
            base = Path(directory)
            result = self.worker_outcome(exit_code=1, forced_stop=True)
            self.assertEqual(self.invoke_main(base, result), 76)
            outcome = json.loads((base / "worker-outcome.json").read_text(encoding="utf-8"))
            self.assertEqual(outcome, {"status": "failed", **result})
            self.assertFalse((base / "supervisor-receipt.json").exists())

    def test_postflight_failure_retains_original_error_and_outcome(self):
        with tempfile.TemporaryDirectory(dir=subject.REPO / "deps") as directory:
            base = Path(directory)
            error = ValueError("invalid result")
            result = self.worker_outcome()
            with self.assertRaises(ValueError) as caught:
                self.invoke_main(base, result, error)
            self.assertIs(caught.exception, error)
            outcome = json.loads((base / "worker-outcome.json").read_text(encoding="utf-8"))
            failure = json.loads((base / "validation-error.json").read_text(encoding="utf-8"))
            self.assertEqual(outcome, {"status": "pending_validation", **result})
            self.assertEqual(failure, {"status": "failed", "error_type": "ValueError", "error": "invalid result"})
            self.assertFalse((base / "supervisor-receipt.json").exists())

    def test_postflight_storage_failure_writes_no_further_record(self):
        with tempfile.TemporaryDirectory(dir=subject.REPO / "deps") as directory:
            base = Path(directory)
            with self.assertRaises(OSError):
                self.invoke_main(base, self.worker_outcome(), OSError(errno.ENOSPC, "full"))
            self.assertTrue((base / "worker-outcome.json").exists())
            self.assertFalse((base / "validation-error.json").exists())
            self.assertFalse((base / "supervisor-receipt.json").exists())

    def test_success_receipt_only_after_validation(self):
        with tempfile.TemporaryDirectory(dir=subject.REPO / "deps") as directory:
            base = Path(directory)
            (base / "sources").mkdir()
            reserve = base / "sources" / "reservation.bin"
            reserve.write_bytes(b"reservation")
            argv = ["supervisor", "--worker-sha256", "a" * 64,
                    "--manifest-sha256", "b" * 64, "--python-sha256", "c" * 64]
            with patch.object(subject.sys, "argv", argv), patch.object(subject, "prepare", return_value=base), \
                    patch.object(subject, "run_worker", return_value=self.worker_outcome()), \
                    patch.object(subject, "validate_result", return_value={"archives": 6}), patch("builtins.print"):
                self.assertEqual(subject.main(), 0)
            outcome = json.loads((base / "worker-outcome.json").read_text(encoding="utf-8"))
            receipt = json.loads((base / "supervisor-receipt.json").read_text(encoding="utf-8"))
            self.assertEqual(outcome["status"], "pending_validation")
            self.assertEqual(receipt["archives"], 6)
            self.assertFalse(reserve.exists())

    def test_masked_storage_failure(self):
        full = OSError(errno.ENOSPC, "full")
        wrapped = RuntimeError("masked")
        wrapped.__context__ = full
        full.__cause__ = wrapped
        self.assertTrue(subject.storage_failure(wrapped))
        group_type = getattr(builtins, "ExceptionGroup", SyntheticGroup)
        self.assertTrue(subject.storage_failure(group_type("group", [RuntimeError("x"), full])))
        self.assertFalse(subject.storage_failure(RuntimeError("other")))

    def test_storage_classifier_without_edquot(self):
        import urllib.error
        had_quota = hasattr(errno, "EDQUOT")
        quota = getattr(errno, "EDQUOT", None)
        if had_quota:
            delattr(errno, "EDQUOT")
        try:
            full = OSError(errno.ENOSPC, "full")
            shared = urllib.error.URLError(full)
            outer = SyntheticGroup("shared", [shared, shared])
            full.__context__ = outer
            self.assertTrue(subject.storage_failure(outer))
            self.assertFalse(subject.storage_failure(OSError(errno.EIO, "other")))
            self.assertFalse(subject.storage_failure(urllib.error.URLError("TLS failure")))
            windows = OSError("quota")
            windows.winerror = 1816
            self.assertTrue(subject.storage_failure(windows))
        finally:
            if had_quota:
                errno.EDQUOT = quota

    def test_worker_has_no_process_or_dynamic_execution_calls(self):
        tree = ast.parse(subject.WORKER.read_text(encoding="utf-8"))
        forbidden = {"eval", "exec", "compile", "__import__", "system", "popen", "Popen"}
        for node in ast.walk(tree):
            if isinstance(node, ast.Call):
                name = node.func.id if isinstance(node.func, ast.Name) else getattr(node.func, "attr", "")
                self.assertNotIn(name, forbidden)
            if isinstance(node, ast.Import):
                self.assertNotIn("subprocess", [item.name for item in node.names])
            if isinstance(node, ast.ImportFrom):
                self.assertNotEqual(node.module, "subprocess")

    def test_timeout_kills_and_reaps_exact_child(self):
        child = FakeProcess(True)
        windows_no_window = 0x08000000
        with tempfile.TemporaryDirectory(dir=subject.REPO / "deps") as directory:
            with patch.object(subject.subprocess, "CREATE_NO_WINDOW", windows_no_window, create=True), \
                    patch.object(subject.subprocess, "Popen", return_value=child) as launch, \
                    patch.object(subject.time, "monotonic", side_effect=[0, 181, 181]):
                result = subject.run_worker(Path(directory))
        self.assertTrue(child.killed)
        self.assertTrue(result["forced_stop"])
        self.assertTrue(result["child_reaped"])
        self.assertEqual(result["child_pid"], 1234)
        self.assertEqual(launch.call_args.args[0][1:4], ["-I", "-S", "-B"])
        self.assertEqual(launch.call_args.kwargs["creationflags"], windows_no_window)

    def test_storage_failure_writes_no_receipt_or_cleanup(self):
        with tempfile.TemporaryDirectory(dir=subject.REPO / "deps") as directory:
            base = Path(directory)
            (base / "sources").mkdir()
            reserve = base / "sources" / "reservation.bin"
            reserve.write_bytes(b"evidence")
            argv = ["supervisor", "--worker-sha256", "a" * 64,
                    "--manifest-sha256", "b" * 64, "--python-sha256", "c" * 64]
            with patch.object(subject.sys, "argv", argv), patch.object(subject, "prepare", return_value=base), \
                    patch.object(subject, "run_worker", return_value={"exit_code": 75, "forced_stop": False,
                                                                        "child_reaped": True, "output_within_limit": True}):
                self.assertEqual(subject.main(), 75)
            self.assertTrue(reserve.exists())
            self.assertFalse((base / "supervisor-receipt.json").exists())
            self.assertFalse((base / "worker-outcome.json").exists())
            self.assertFalse((base / "validation-error.json").exists())

    def test_result_requires_all_matching_sources(self):
        with tempfile.TemporaryDirectory(dir=subject.REPO / "deps") as directory:
            base = Path(directory)
            source = base / "sources"
            source.mkdir()
            (source / "reservation.bin").write_bytes(b"")
            worker, manifest, interpreter = base / "worker.py", base / "manifest.json", base / "python.exe"
            worker.write_text("worker", encoding="utf-8")
            interpreter.write_text("python", encoding="utf-8")
            entries = []
            for index in range(6):
                name = f"s{index}.tar.gz"
                (source / name).write_bytes(b"x")
                entries.append({"name": name, "size": 1, "sha256": hashlib.sha256(b"x").hexdigest()})
            manifest.write_text(json.dumps(entries), encoding="utf-8")
            (base / "stdout.json").write_text(json.dumps({"transferred": 6, "expanded": 20,
                                                           "metadata": {e["name"]: {} for e in entries}}), encoding="utf-8")
            with patch.object(subject, "WORKER", worker), patch.object(subject, "MANIFEST", manifest), \
                    patch.object(subject.sys, "executable", str(interpreter)), \
                    patch.object(subject, "EXPECTED_TRANSFER", 6), patch.object(subject, "RESERVE_BYTES", 0):
                args = (subject.digest(worker), subject.digest(manifest), subject.digest(interpreter))
                self.assertEqual(subject.validate_result(base, *args)["archives"], 6)
                (source / entries[0]["name"]).write_bytes(b"y")
                with self.assertRaises(ValueError):
                    subject.validate_result(base, *args)


if __name__ == "__main__":
    unittest.main()
