"""Capture QGA status response bytes before decoding or making another request."""
from datetime import datetime, timezone
import json
import os
from pathlib import Path

# QEMU limits each captured stream to 16 MiB. Two base64 streams need less
# than 43 MiB; retain headroom for the JSON wrapper.
# https://gitlab.com/qemu-project/qemu/-/blob/43f2def68476697deb0d119cbae51b20019c6c86/qga/commands.c
MAX_BODY = 48 * 1024 * 1024
CHUNK = 64 * 1024


def write_once(path, data):
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "wb") as output:
        output.write(data)
        output.flush()
        os.fsync(output.fileno())


def read_status(path):
    with path.open("rb") as stream:
        response = json.load(stream)
    status = response.get("data") if isinstance(response, dict) else None
    if not isinstance(status, dict) or status.get("exited") not in (True, 1, False, 0):
        raise ValueError("invalid guest status response")
    return status


def stream_response(path, open_response):
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "wb") as output:
        with open_response() as response:
            total = 0
            while True:
                chunk = response.read(min(CHUNK, MAX_BODY - total + 1))
                if not chunk:
                    break
                allowed = min(len(chunk), MAX_BODY - total)
                output.write(chunk[:allowed])
                total += allowed
                if allowed != len(chunk):
                    output.flush()
                    os.fsync(output.fileno())
                    raise ValueError("response exceeds limit; partial body preserved")
        output.flush()
        os.fsync(output.fileno())


def poll_preserving_terminal(pid, receipt_path, open_response):
    """Serialize polling in a private task directory; preserve uncertain state."""
    if type(pid) is not int or pid <= 0:
        raise ValueError("invalid guest PID")
    path = Path(receipt_path)
    marker = path.with_name(path.name + ".polling")
    complete = path.with_name(path.name + ".complete")
    if complete.exists():
        observed = json.loads(complete.read_text(encoding="ascii"))
        status = read_status(path)
        if observed.get("pid") != pid or not status["exited"]:
            raise RuntimeError("saved status identity or terminal state mismatch")
        return status
    if marker.exists() or path.exists() or marker.is_symlink() or path.is_symlink():
        raise RuntimeError("prior status response is incomplete or uncertain; do not poll")
    started = {"pid": pid, "requested_utc": datetime.now(timezone.utc).isoformat()}
    write_once(marker, json.dumps(started, separators=(",", ":")).encode("ascii"))
    stream_response(path, open_response)
    observed = {"pid": pid, "observed_utc": datetime.now(timezone.utc).isoformat()}
    write_once(complete, json.dumps(observed, separators=(",", ":")).encode("ascii"))
    status = read_status(path)
    if not status["exited"]:
        complete.unlink()
        path.unlink()
        marker.unlink()
    return status
