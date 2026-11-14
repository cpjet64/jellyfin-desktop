"""Strict v2 receipt consumer; production admission remains deliberately closed."""
from dataclasses import asdict
import json
import ntpath

from build_only.receipt import (_keys, _number, _object, _sha,
                                _validate_command, _validate_outputs)
from .profile import admit_production, profile_hash, validate_profile

FIELDS = ("schema", "source", "profile", "endpoint", "env_digest", "command",
          "root", "deadlines", "pins", "baseline", "before", "final",
          "verified_all_ms", "intervention_ms", "residuals", "result",
          "owned", "emergency_cleanup", "outputs", "finalization", "errors")


def validate_receipt(raw, *, expected_profile, expected_source, observed_exit,
                     observed_receipt_closed):
    """Validate only against independently supplied profile and outer observations."""
    admit_production(expected_profile)
    return _validate_fields(raw, expected_profile=expected_profile, expected_source=expected_source,
                            observed_exit=observed_exit, observed_receipt_closed=observed_receipt_closed)


def _validate_fields(raw, *, expected_profile, expected_source, observed_exit, observed_receipt_closed):
    """Field-level checks only; this function never certifies production admission."""
    validate_profile(expected_profile)
    record = json.loads(raw, object_pairs_hook=_object)
    _keys(record, FIELDS)
    policy = expected_profile.command_policy
    _sha(expected_source)
    if (record["schema"] != "build-only-v2" or record["source"] != expected_source
            or expected_source != policy.source_sha256
            or record["profile"] != profile_hash(expected_profile)
            or record["endpoint"] != expected_profile.endpoint
            or record["env_digest"] != expected_profile.env_digest):
        raise ValueError("profile or source mismatch")
    if type(observed_exit) is not int or observed_exit != 0 or observed_receipt_closed is not True:
        raise ValueError("outer process or receipt failed")
    _validate_command(record, policy)
    _validate_lifecycle(record, policy)
    _validate_members(record, expected_profile)
    _validate_outputs(record["outputs"], policy.command.outputs)
    return record


def _validate_lifecycle(record, policy):
    root = record["root"]
    _keys(root, ("pid", "creation_time", "exit", "signaled", "observed_ms", "closed"))
    for key in ("pid", "creation_time"):
        _number(root[key], 1)
    _number(root["observed_ms"])
    if (type(root["exit"]) is not int or root["exit"] != 0
            or root["signaled"] is not True or root["closed"] is not True):
        raise ValueError("root failed")
    d = record["deadlines"]
    _keys(d, ("start_ms", "root_deadline_ms", "grace_deadline_ms", "cleanup_start_ms",
              "cleanup_deadline_ms", "settled_ms"))
    for value in d.values():
        _number(value)
    if (d["root_deadline_ms"] != d["start_ms"] + policy.deadline_ms
            or not d["start_ms"] <= root["observed_ms"] < d["root_deadline_ms"]
            or d["grace_deadline_ms"] != min(d["root_deadline_ms"], root["observed_ms"] + policy.grace_ms)
            or d["cleanup_deadline_ms"] != d["cleanup_start_ms"] + 10000
            or not root["observed_ms"] <= d["cleanup_start_ms"] < d["root_deadline_ms"]
            or not d["cleanup_start_ms"] <= d["settled_ms"] < d["cleanup_deadline_ms"]):
        raise ValueError("deadline mismatch")
    owned = record["owned"]
    _keys(owned, ("opened", "closed", "failed"))
    for value in owned.values():
        _number(value)
    if owned["opened"] < 1 or owned["opened"] != owned["closed"] or owned["failed"]:
        raise ValueError("handle accounting failed")
    _keys(record["finalization"], ("exclusive", "write_complete"))
    if (any(value is not True for value in record["finalization"].values())
            or record["emergency_cleanup"] is not False or record["errors"] != []):
        raise ValueError("receipt finalization failed")


def _validate_members(record, profile):
    expected = [asdict(pin) for pin in profile.residual_roles]
    if record["pins"] != expected:
        raise ValueError("pinned profile mismatch")
    for name in ("baseline", "before", "final"):
        _keys(record[name], ("total", "active"))
        for value in record[name].values():
            _number(value)
    baseline, before, final = (record[name] for name in ("baseline", "before", "final"))
    residuals = record["residuals"]
    if (not isinstance(residuals, list) or len(residuals) != baseline["active"]
            or baseline["total"] < 1 + baseline["active"]
            or baseline["total"] != before["total"] or baseline["total"] != final["total"]
            or before["active"] > baseline["active"] or final["active"] != 0):
        raise ValueError("incomplete or changed membership")
    for field in ("verified_all_ms", "intervention_ms"):
        _number(record[field])
    d = record["deadlines"]
    if not (d["cleanup_start_ms"] <= record["verified_all_ms"] <= record["intervention_ms"]
            <= d["settled_ms"] < d["cleanup_deadline_ms"]
            and (not residuals or record["intervention_ms"] >= d["grace_deadline_ms"])):
        raise ValueError("verification or intervention ordering")
    roles, pids = set(), set()
    for item in residuals:
        _validate_member(item, expected, record["root"], roles, pids)
    result = "natural" if not residuals else ("build_service_cleanup" if "mspdbsrv" in roles else "telemetry_cleanup")
    if record["result"] != result:
        raise ValueError("cleanup classification mismatch")


def _validate_member(item, expected, root, roles, pids):
    _keys(item, ("pid", "role", "identity", "member", "action", "signaled", "closed"))
    _number(item["pid"], 1)
    identity = item["identity"]
    _keys(identity, ("creation_time", "process_path", "retained_final_path", "sha256", "size", "file_id", "expected_version"))
    _number(identity["creation_time"], 1)
    _number(identity["size"], 1)
    matches = [pin for pin in expected if pin["role"] == item["role"] and
               all(pin[key] == identity[key] for key in ("sha256", "size", "file_id")) and
               pin["version"] == identity["expected_version"] and
               _path(pin["path"]) == _path(identity["process_path"]) == _path(identity["retained_final_path"])]
    if (len(matches) != 1 or item["role"] in roles or item["pid"] in pids
            or (item["pid"], identity["creation_time"]) == (root["pid"], root["creation_time"])
            or item["member"] is not True or item["action"] not in ("terminate", "already_exited")
            or item["signaled"] is not True or item["closed"] is not True):
        raise ValueError("unverified or unclosed residual")
    roles.add(item["role"])
    pids.add(item["pid"])


def _path(value):
    if not isinstance(value, str) or not value:
        raise ValueError("invalid observed path")
    if value.startswith("\\\\?\\UNC\\"):
        value = "\\\\" + value[8:]
    elif value.startswith("\\\\?\\"):
        value = value[4:]
    return ntpath.normcase(ntpath.normpath(value))
