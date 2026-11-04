"""Prepare one bounded Windows-only static sdist inspection process."""

import argparse
import errno
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[2]
BASE = REPO / "deps" / "w463-sdist-proof"
WORKER = Path(__file__).with_name("macos_sdist_static.py")
MANIFEST = Path(__file__).with_name("macos_sdist_manifest.json")
RESERVE_BYTES = 480 * 1024 * 1024
HEADROOM_BYTES = 2 * 1024 * 1024 * 1024
OUTPUT_LIMIT = 16 * 1024 * 1024
ERROR_LIMIT = 1024 * 1024
DEADLINE_SECONDS = 180
EXPECTED_TRANSFER = 5421756


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def storage_failure(error):
    pending, seen = [error], set()
    storage_codes = {errno.ENOSPC}
    quota = getattr(errno, "EDQUOT", None)
    if quota is not None:
        storage_codes.add(quota)
    while pending:
        current = pending.pop()
        if id(current) in seen:
            continue
        seen.add(id(current))
        if len(seen) > 256:
            return True
        if isinstance(current, OSError) and (
                current.errno in storage_codes or
                getattr(current, "winerror", None) in (39, 112, 1816)):
            return True
        for value in (getattr(current, "__cause__", None), getattr(current, "__context__", None),
                      getattr(current, "reason", None)):
            if isinstance(value, BaseException):
                pending.append(value)
        pending.extend(value for value in getattr(current, "exceptions", ()) if isinstance(value, BaseException))
    return False


def prepare(expected_worker, expected_manifest, expected_python):
    if os.name != "nt":
        raise RuntimeError("Windows supervisor required")
    for value in (expected_worker, expected_manifest, expected_python):
        if not re.fullmatch(r"[0-9a-f]{64}", value):
            raise ValueError("expected SHA-256 must be lowercase hexadecimal")
    if (digest(WORKER) != expected_worker or digest(MANIFEST) != expected_manifest or
            digest(Path(sys.executable)) != expected_python):
        raise ValueError("reviewed input hash mismatch")
    deps = REPO / "deps"
    for path in (REPO, deps):
        info = path.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError("reparse point in workspace ancestry")
    if shutil.disk_usage(deps).free < RESERVE_BYTES + HEADROOM_BYTES:
        raise ValueError("insufficient free space and headroom")
    BASE.mkdir()
    return BASE


def run_worker(base):
    output = base / "stdout.json"
    error = base / "stderr.log"
    target = base / "sources"
    command = [sys.executable, "-I", "-S", "-B", str(WORKER), "--acquire-and-inspect", str(target)]
    started = time.monotonic()
    with output.open("xb") as stdout, error.open("xb") as stderr:
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=stdout,
                                   stderr=stderr, close_fds=True,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        timed_out = False
        try:
            while process.poll() is None:
                if (time.monotonic() - started >= DEADLINE_SECONDS or
                        output.stat().st_size > OUTPUT_LIMIT or error.stat().st_size > ERROR_LIMIT):
                    timed_out = True
                    process.kill()
                    break
                time.sleep(0.1)
            code = process.wait(timeout=5)
        except BaseException:
            process.kill()
            process.wait(timeout=5)
            raise
    bounded = output.stat().st_size <= OUTPUT_LIMIT and error.stat().st_size <= ERROR_LIMIT
    return {"child_pid": process.pid, "exit_code": code, "forced_stop": timed_out,
            "child_reaped": process.poll() is not None,
            "output_within_limit": bounded,
            "elapsed_seconds": round(time.monotonic() - started, 3),
            "stdout_bytes": output.stat().st_size, "stderr_bytes": error.stat().st_size}


def validate_result(base, expected_worker, expected_manifest, expected_python):
    if (digest(WORKER) != expected_worker or digest(MANIFEST) != expected_manifest or
            digest(Path(sys.executable)) != expected_python):
        raise ValueError("reviewed input changed during inspection")
    entries = json.loads(MANIFEST.read_text(encoding="utf-8"))
    output = base / "stdout.json"
    if output.stat().st_size > OUTPUT_LIMIT:
        raise ValueError("output limit exceeded")
    report = json.loads(output.read_text(encoding="utf-8"))
    names = {entry["name"] for entry in entries}
    if (len(entries) != 6 or len(names) != 6 or sum(entry["size"] for entry in entries) != EXPECTED_TRANSFER or
            report.get("transferred") != EXPECTED_TRANSFER or
            not 0 < report.get("expanded", 0) < 256 * 1024 * 1024 or
            set(report.get("metadata", {})) != names):
        raise ValueError("incomplete or invalid inspection report")
    source = base / "sources"
    if {path.name for path in source.iterdir()} != names | {"reservation.bin"}:
        raise ValueError("unexpected workspace contents")
    if (source / "reservation.bin").stat().st_size != RESERVE_BYTES:
        raise ValueError("reservation size changed")
    for entry in entries:
        path = source / entry["name"]
        info = path.lstat()
        if (not stat.S_ISREG(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400 or
                info.st_size != entry["size"] or digest(path) != entry["sha256"]):
            raise ValueError("source identity mismatch")
    return {"inspection_sha256": digest(output), "archives": len(entries),
            "transferred": report["transferred"], "expanded": report["expanded"]}


def persist_record(path, record):
    with path.open("x", encoding="utf-8") as output:
        json.dump(record, output, indent=2)
        output.flush()
        os.fsync(output.fileno())


def validate_with_evidence(base, args):
    try:
        return validate_result(base, args.worker_sha256, args.manifest_sha256, args.python_sha256)
    except BaseException as error:
        if storage_failure(error):
            raise
        record = {"status": "failed", "error_type": type(error).__name__, "error": str(error)}
        try:
            persist_record(base / "validation-error.json", record)
        except BaseException as record_error:
            if storage_failure(record_error):
                raise
            raise error from record_error
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--worker-sha256", required=True)
    parser.add_argument("--manifest-sha256", required=True)
    parser.add_argument("--python-sha256", required=True)
    args = parser.parse_args()
    base = prepare(args.worker_sha256, args.manifest_sha256, args.python_sha256)
    result = run_worker(base)
    if result["exit_code"] == 75:
        return 75
    failed = (result["forced_stop"] or not result["child_reaped"] or
              not result["output_within_limit"] or result["exit_code"] != 0)
    persist_record(base / "worker-outcome.json", {"status": "failed" if failed else "pending_validation", **result})
    if result["forced_stop"]:
        return 76
    if not result["child_reaped"] or not result["output_within_limit"]:
        return 77
    if result["exit_code"] != 0:
        return result["exit_code"] if 0 < result["exit_code"] < 256 else 1
    result.update(validate_with_evidence(base, args))
    result.update({"worker_sha256": args.worker_sha256, "manifest_sha256": args.manifest_sha256,
                   "python_sha256": args.python_sha256,
                   "reservation_bytes": RESERVE_BYTES, "scope": "single reviewed worker; no children"})
    receipt = base / "supervisor-receipt.json"
    with receipt.open("x", encoding="utf-8") as output:
        json.dump(result, output, indent=2)
    (base / "sources" / "reservation.bin").unlink()
    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BaseException as error:
        if storage_failure(error):
            os._exit(75)
        raise
