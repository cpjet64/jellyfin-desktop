"""Single-process QGA journal. Transport adapter must supply submit and poll.

No transport is selected here: never attach a guest until coordinator review.
"""
import base64
import hashlib
import json
import os
from pathlib import Path
import re

TOTAL = 156208
SHA256 = "ee9daf887e9a2841f22b3dda9dedeaa203d278eabdf570aea3177fdbd178f07c"
CHUNK = 65536
SOURCE = "8358bd61286fb2d706f245a278aa577ef27363e1"


def write_once(path, value):
    with path.open("x", encoding="utf-8", newline="\n") as output:
        json.dump(value, output, sort_keys=True)
        output.write("\n")
        output.flush()
        os.fsync(output.fileno())


def source_digest(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def chunk_plan(bundle, offset):
    """Read a source slice and calculate the complete prefix before and after."""
    if bundle.stat().st_size != TOTAL or source_digest(bundle) != SHA256:
        raise ValueError("local source bundle changed")
    if offset < 0 or offset >= TOTAL or offset % CHUNK:
        raise ValueError("invalid offset")
    before = hashlib.sha256()
    with bundle.open("rb") as stream:
        remaining = offset
        while remaining:
            data = stream.read(min(1024 * 1024, remaining))
            if not data:
                raise ValueError("short source")
            before.update(data)
            remaining -= len(data)
        data = stream.read(min(CHUNK, TOTAL - offset))
    after = before.copy()
    after.update(data)
    return {"offset": offset, "end": offset + len(data), "before": before.hexdigest(),
            "after": after.hexdigest(), "encoded": base64.b64encode(data).decode("ascii")}


def _submit_once(journal, sequence, operation, args, target, submit):
    """Write durable intent before submission; uncertainty prohibits resubmission."""
    prefix = journal / ("%04d" % sequence)
    intent_path = prefix.with_suffix(".intent.json")
    intent = {"operation": operation, "args_sha256": hashlib.sha256(
        json.dumps(args, sort_keys=True).encode()).hexdigest(), "target": target, "source": SOURCE}
    write_once(intent_path, intent)
    try:
        pid = submit(operation, args)
        if type(pid) is not int or pid <= 0:
            raise ValueError("invalid guest PID")
    except BaseException:
        write_once(prefix.with_suffix(".unknown.json"), {"retry_forbidden": True})
        raise
    write_once(prefix.with_suffix(".pid.json"), {"pid": pid, "target": target})
    return pid


def _settle_once(journal, sequence, target, poll, expected):
    """Only poll the recorded PID; require exact receipt before the next chunk."""
    prefix = journal / ("%04d" % sequence)
    intent = json.loads(prefix.with_suffix(".intent.json").read_text(encoding="utf-8"))
    pid = json.loads(prefix.with_suffix(".pid.json").read_text(encoding="utf-8"))
    if intent["target"] != target or pid["target"] != target:
        raise ValueError("target changed")
    try:
        status = poll(pid["pid"])
    except BaseException as exc:
        if getattr(exc, "terminal", False):
            write_once(prefix.with_suffix(".failure.json"), {"retry_forbidden": True,
                                                              "status": getattr(exc, "metadata", {})})
        raise
    if status is None:
        return False
    if status != expected:
        write_once(prefix.with_suffix(".failure.json"), {"retry_forbidden": True})
        raise ValueError("unexpected guest receipt")
    write_once(prefix.with_suffix(".receipt.json"), status)
    return True


def build_plans(bundle):
    """Read the bounded source once and build every exact prefix receipt."""
    if bundle.stat().st_size != TOTAL:
        raise ValueError("source bundle size changed")
    digest = hashlib.sha256()
    plans = []
    with bundle.open("rb") as stream:
        while data := stream.read(CHUNK):
            before = digest.hexdigest()
            digest.update(data)
            plans.append({"offset": len(plans) * CHUNK, "end": len(plans) * CHUNK + len(data),
                          "before": before, "after": digest.hexdigest(),
                          "encoded": base64.b64encode(data).decode("ascii")})
    if digest.hexdigest() != SHA256:
        raise ValueError("source bundle hash changed")
    return plans


def reject_later_entries(journal, sequence):
    for item in journal.iterdir():
        match = re.fullmatch(r"(\d{4})\.(?:intent|pid|receipt|unknown|failure)\.json", item.name)
        if match and int(match.group(1)) >= sequence:
            raise RuntimeError("noncontiguous or orphan journal entry")


def _planned_step(journal, bundle, target, plans=None):
    """Require a contiguous journal; return one pending or next safe operation."""
    empty = hashlib.sha256(b"").hexdigest()
    sequence = 0
    offset = 0
    if plans is None:
        plans = build_plans(bundle)
    while True:
        prefix = journal / ("%04d" % sequence)
        intent = prefix.with_suffix(".intent.json")
        receipt = prefix.with_suffix(".receipt.json")
        if prefix.with_suffix(".unknown.json").exists() or prefix.with_suffix(".failure.json").exists():
            raise RuntimeError("ambiguous or failed operation; stop")
        if sequence == 0:
            operation, args, expected = "init", [], {"offset": 0, "sha256": empty}
        elif offset < TOTAL:
            plan = plans[sequence - 1]
            operation = "append"
            args = [str(offset), plan["before"], plan["encoded"]]
            expected = {"offset": plan["end"], "sha256": plan["after"]}
        else:
            operation, args, expected = "finish", [], {"offset": TOTAL, "sha256": SHA256}
        if not intent.exists():
            reject_later_entries(journal, sequence)
            return sequence, operation, args, expected, False
        entry = json.loads(intent.read_text(encoding="utf-8"))
        if entry.get("target") != target:
            raise RuntimeError("journal target differs from current target")
        args_digest = hashlib.sha256(json.dumps(args, sort_keys=True).encode()).hexdigest()
        if entry.get("operation") != operation or entry.get("args_sha256") != args_digest or entry.get("source") != SOURCE:
            raise RuntimeError("intent differs from planned source operation")
        pid_path = prefix.with_suffix(".pid.json")
        if not pid_path.exists():
            raise RuntimeError("intent has no recorded PID; submission outcome unknown")
        pid = json.loads(pid_path.read_text(encoding="utf-8"))
        if pid.get("target") != entry.get("target") or type(pid.get("pid")) is not int or pid["pid"] <= 0:
            raise RuntimeError("journal PID identity mismatch")
        if not receipt.exists():
            reject_later_entries(journal, sequence + 1)
            return sequence, operation, args, expected, True
        observed = json.loads(receipt.read_text(encoding="utf-8"))
        if observed != expected:
            raise RuntimeError("receipt does not match expected source")
        if operation == "finish":
            reject_later_entries(journal, sequence + 1)
            return None
        offset = expected["offset"]
        sequence += 1


class Session:
    """Fresh authorized run only; an interrupted session must never be resumed."""
    def __init__(self, journal, bundle, target, authorize_fresh):
        if not callable(authorize_fresh) or authorize_fresh(target) is not True:
            raise PermissionError('independent fresh-run authorization required')
        self.journal = Path(journal)
        self.journal.mkdir(mode=0o700)  # Existing journals are never reopened.
        self.bundle, self.target = Path(bundle), target
        self.plans = build_plans(self.bundle)
        self.active = True

    def step(self, submit, poll):
        if not self.active:
            raise RuntimeError('session closed; no restart or retry')
        try:
            plan = _planned_step(self.journal, self.bundle, self.target, self.plans)
            if plan is None:
                self.active = False
                return True
            sequence, operation, args, expected, pending = plan
            if not pending:
                _submit_once(self.journal, sequence, operation, args, self.target, submit)
            return _settle_once(self.journal, sequence, self.target, poll, expected) and False
        except BaseException:
            self.active = False
            raise
