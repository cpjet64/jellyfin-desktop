"""Preserve a QGA terminal response before interpreting its contents."""
from datetime import datetime, timezone
import json
import os
from pathlib import Path


def poll_preserving_terminal(pid, receipt_path, request_status):
    """Return status; replay a saved terminal response without another request.

    The caller serializes polling and provides an existing private task-owned
    directory and a unique receipt path for this PID/operation. A malformed or
    incomplete existing receipt is a hard stop, never a reason to poll again.
    """
    if type(pid) is not int or pid <= 0:
        raise ValueError("invalid guest PID")
    path = Path(receipt_path)
    if path.exists() or path.is_symlink():
        saved = json.loads(path.read_text(encoding="utf-8"))
        if saved.get("pid") != pid or not isinstance(saved.get("response"), dict):
            raise ValueError("terminal receipt identity mismatch")
        status = saved["response"].get("data")
        if not isinstance(status, dict) or status.get("exited") not in (True, 1):
            raise ValueError("terminal receipt incomplete")
        return status
    marker = path.with_name(path.name + ".polling")
    descriptor = os.open(marker, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8") as intent:
        intent.write(str(pid) + "\n")
        intent.flush()
        os.fsync(intent.fileno())
    response = request_status()
    status = response.get("data") if isinstance(response, dict) else None
    if not isinstance(status, dict):
        raise ValueError("invalid guest status response")
    if status.get("exited") in (False, 0, None):
        marker.unlink()
        return status
    if status.get("exited") not in (True, 1):
        raise ValueError("invalid guest exit state")
    record = {"pid": pid, "observed_utc": datetime.now(timezone.utc).isoformat(),
              "response": response}
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as output:
        json.dump(record, output, ensure_ascii=True, separators=(",", ":"))
        output.write("\n")
        output.flush()
        os.fsync(output.fileno())
    return status
