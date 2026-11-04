import ast
import errno
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import macos_sdist_supervise as subject


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
    def test_masked_storage_failure(self):
        full = OSError(errno.ENOSPC, "full")
        wrapped = RuntimeError("masked")
        wrapped.__context__ = full
        full.__cause__ = wrapped
        self.assertTrue(subject.storage_failure(wrapped))
        self.assertTrue(subject.storage_failure(ExceptionGroup("group", [RuntimeError("x"), full])))
        self.assertFalse(subject.storage_failure(RuntimeError("other")))

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
        with tempfile.TemporaryDirectory(dir=subject.REPO / "deps") as directory:
            with patch.object(subject.subprocess, "Popen", return_value=child) as launch, \
                    patch.object(subject.time, "monotonic", side_effect=[0, 181, 181]):
                result = subject.run_worker(Path(directory))
        self.assertTrue(child.killed)
        self.assertTrue(result["forced_stop"])
        self.assertTrue(result["child_reaped"])
        self.assertEqual(result["child_pid"], 1234)
        self.assertEqual(launch.call_args.args[0][1:4], ["-I", "-S", "-B"])
        self.assertEqual(launch.call_args.kwargs["creationflags"], subject.subprocess.CREATE_NO_WINDOW)

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
