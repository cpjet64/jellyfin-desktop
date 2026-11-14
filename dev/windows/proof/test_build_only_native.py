"""Explicitly allocated Windows qualification through the strict outer helper."""
from dataclasses import asdict
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import unittest

ENABLED = sys.platform == "win32" and os.environ.get("BUILD_ONLY_NATIVE_QUALIFY") == "1"
HELPER_SHA = "7ae01dfd262a18bf863c057d570993b2c4dc529fcc03ac1e58d15f0dedb0ebd5"


def _hash(path):
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _exclusive_read(path):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.restype = ctypes.c_void_p
    kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                   ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    kernel.ReadFile.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32,
                                ctypes.POINTER(ctypes.c_uint32), ctypes.c_void_p]
    handle = kernel.CreateFileW(str(path), 0x80000000, 0, None, 3, 0x80, None)
    if handle == ctypes.c_void_p(-1).value:
        raise OSError(ctypes.get_last_error(), "exclusive receipt open failed")
    try:
        chunks = []
        while True:
            buffer = ctypes.create_string_buffer(65536)
            count = ctypes.c_uint32()
            if not kernel.ReadFile(handle, buffer, len(buffer), ctypes.byref(count), None):
                raise OSError(ctypes.get_last_error(), "receipt read failed")
            if not count.value:
                break
            chunks.append(buffer.raw[:count.value])
        data = b"".join(chunks)
    finally:
        if not kernel.CloseHandle(handle):
            raise OSError(ctypes.get_last_error(), "exclusive receipt close failed")
    return data


def _strict_receipt(path, expected_exit):
    data = path.read_text(encoding="ascii")
    fields = data.splitlines()
    patterns = (r"receipt_version=1", r"root_pid=[1-9][0-9]*", "root_exit=" + str(expected_exit),
                r"timeout=0 active=0", r"handles_before=[0-9]+ handles_after=[0-9]+ measured=1",
                r"owned_opened=6 owned_closed=6 close_failed=0", r"handles_complete=1",
                "final_result=" + str(expected_exit), r"proof_complete=1")
    if len(fields) != len(patterns) or any(not re.fullmatch(pattern, field)
                                           for pattern, field in zip(patterns, fields)):
        raise AssertionError("outer helper receipt malformed")


@unittest.skipUnless(ENABLED, "explicit Windows qualification flag required")
class NativeQualificationTests(unittest.TestCase):
    def test_private_job_cases(self):
        from build_only.fixture_policy import make_policy
        from build_only.receipt import validate_receipt

        root = Path(os.environ["BUILD_ONLY_NATIVE_ROOT"])
        helper = Path(os.environ["BUILD_ONLY_NATIVE_HELPER"])
        if not root.is_absolute() or not helper.is_absolute() or not root.is_dir():
            self.fail("explicit absolute qualification paths required")
        for mode in ("natural", "residual", "rootfail"):
            self._case(root, helper, mode, make_policy, validate_receipt)

    def _case(self, root, helper, mode, make_policy, validate_receipt):
        if _hash(helper) != HELPER_SHA:
            self.fail("outer helper identity mismatch")
        case = root / mode
        case.mkdir(exist_ok=False)
        policy = make_policy(case, mode)
        manifest = case / "manifest.json"
        with manifest.open("x", encoding="utf-8") as stream:
            json.dump(asdict(policy), stream, separators=(",", ":"))
        strict = case / "strict.receipt"
        build = case / "build.receipt"
        args = [str(helper), str(strict), "25000", str(Path(__file__).resolve().parent),
                sys.executable, "-B", "-m", "build_only.fixture_runner", str(manifest), str(build)]
        with (case / "helper.stdout").open("xb") as out, (case / "helper.stderr").open("xb") as err:
            process = subprocess.Popen(args, stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                code = process.wait(timeout=38)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=10)
                raise
        self.assertEqual(code, 1 if mode == "rootfail" else 0)
        _strict_receipt(strict, code)
        raw = _exclusive_read(build)
        if mode == "rootfail":
            failed = json.loads(raw)
            self.assertEqual(failed["schema"], "build-only-failed-v1")
            outcome = failed["evidence"]["outcome"]
            self.assertEqual(outcome["root"]["exit"], 7)
            self.assertIs(outcome["root"]["signaled"], True)
            self.assertIn("root failed", outcome["error"])
            self.assertIs(outcome["emergency"], True)
            self.assertEqual(outcome["emergency_final"]["active"], 0)
            self.assertIs(outcome["root"]["closed"], True)
        else:
            receipt = validate_receipt(raw, expected_source=policy.source_sha256, expected_policy=policy,
                                       observed_exit=code, observed_receipt_closed=True, production=False)
            self.assertEqual(receipt["result"], "natural" if mode == "natural" else "telemetry_cleanup")
