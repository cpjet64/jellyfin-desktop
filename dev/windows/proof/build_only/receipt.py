"""Strict build-only receipt validation against an independently pinned policy."""
from dataclasses import asdict
import json
import re
from .policy import policy_hash, validate_command

FIELDS = ("schema", "source", "policy", "policy_kind", "result", "command",
          "root", "deadlines", "file_pin", "residuals", "baseline", "before",
          "final", "owned", "forced_cleanup", "emergency_cleanup", "outputs",
          "finalization", "errors")


def _object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate key")
        result[key] = value
    return result


def _keys(value, names):
    if not isinstance(value, dict) or tuple(value) != names:
        raise ValueError("invalid fields or order")


def _number(value, minimum=0):
    if type(value) is not int or value < minimum:
        raise ValueError("invalid integer")


def _sha(value):
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value):
        raise ValueError("invalid digest")


def validate_receipt(raw: bytes, *, expected_source: str, expected_policy,
                     observed_exit: int, observed_receipt_closed: bool,
                     production: bool = True) -> dict:
    record = json.loads(raw, object_pairs_hook=_object)
    _keys(record, FIELDS)
    validate_command(expected_policy, expected_policy.command)
    _sha(expected_source)
    if expected_source != expected_policy.source_sha256 or record["source"] != expected_source:
        raise ValueError("source mismatch")
    if record["schema"] != "build-only-v1" or record["policy"] != policy_hash(expected_policy):
        raise ValueError("schema or policy mismatch")
    if record["policy_kind"] != expected_policy.kind or (production and expected_policy.kind != "production"):
        raise ValueError("policy kind mismatch")
    if type(observed_exit) is not int or observed_exit != 0 or observed_receipt_closed is not True:
        raise ValueError("helper exit not successful")
    _validate_command(record, expected_policy)
    _validate_lifecycle(record, expected_policy)
    _validate_settlement(record, expected_policy)
    _validate_outputs(record["outputs"], expected_policy.command.outputs)
    return record


def _validate_command(record, policy):
    expected = asdict(policy.command)
    expected["args"] = list(policy.command.args)
    expected["outputs"] = list(policy.command.outputs)
    expected["env_names"] = list(policy.command.env_names)
    _keys(record["command"], tuple(expected))
    _number(record["command"]["executable_size"], 1)
    if record["command"] != expected:
        raise ValueError("command identity mismatch")


def _validate_lifecycle(record, policy):
    root = record["root"]
    _keys(root, ("pid", "creation_time", "exit", "signaled", "observed_ms", "closed"))
    _number(root["pid"], 1)
    _number(root["creation_time"], 1)
    _number(root["observed_ms"])
    if (type(root["exit"]) is not int or root["exit"] != 0 or root["signaled"] is not True
            or root["closed"] is not True):
        raise ValueError("root evidence failed")
    deadlines = record["deadlines"]
    _keys(deadlines, ("start_ms", "root_deadline_ms", "grace_deadline_ms", "cleanup_start_ms", "cleanup_deadline_ms", "settled_ms"))
    for value in deadlines.values():
        _number(value)
    if (deadlines["root_deadline_ms"] != deadlines["start_ms"] + policy.deadline_ms
            or not deadlines["start_ms"] <= root["observed_ms"] < deadlines["root_deadline_ms"]
            or deadlines["grace_deadline_ms"] != min(deadlines["root_deadline_ms"], root["observed_ms"] + policy.grace_ms)
            or deadlines["cleanup_deadline_ms"] != deadlines["cleanup_start_ms"] + 10000
            or not root["observed_ms"] <= deadlines["cleanup_start_ms"]
            or deadlines["cleanup_start_ms"] >= deadlines["root_deadline_ms"]
            or not deadlines["cleanup_start_ms"] <= deadlines["settled_ms"] < deadlines["cleanup_deadline_ms"]
            or (record["result"] == "telemetry_cleanup" and deadlines["cleanup_start_ms"] < deadlines["grace_deadline_ms"])):
        raise ValueError("deadline mismatch")
    _keys(record["owned"], ("opened", "closed", "failed"))
    for value in record["owned"].values():
        _number(value)
    if (record["owned"]["opened"] < 1 or record["owned"]["opened"] != record["owned"]["closed"]
            or record["owned"]["failed"] != 0):
        raise ValueError("owned handles not closed")
    _keys(record["finalization"], ("exclusive", "write_complete"))
    if any(value is not True for value in record["finalization"].values()) or record["errors"] != []:
        raise ValueError("finalization failed")
    if record["emergency_cleanup"] is not False:
        raise ValueError("emergency cleanup")


def _validate_settlement(record, policy):
    for field in ("baseline", "before", "final"):
        _keys(record[field], ("total", "active"))
        for value in record[field].values():
            _number(value)
    if (record["baseline"]["total"] != record["before"]["total"]
            or record["baseline"]["total"] != record["final"]["total"]
            or record["baseline"]["total"] < 1 + record["baseline"]["active"]
            or any(record[field]["active"] > record[field]["total"] for field in ("baseline", "before", "final"))
            or record["before"]["active"] > record["baseline"]["active"]
            or record["final"]["active"] != 0):
        raise ValueError("new association or unsettled job")
    pin = {"path": policy.telemetry_path, "sha256": policy.telemetry_sha256,
           "size": policy.telemetry_size, "version": policy.telemetry_version,
           "file_id": policy.telemetry_file_id}
    _keys(record["file_pin"], tuple(pin))
    _number(record["file_pin"]["size"], 1)
    if record["file_pin"] != pin:
        raise ValueError("file pin mismatch")
    residuals = record["residuals"]
    if not isinstance(residuals, list) or len(residuals) != record["baseline"]["active"]:
        raise ValueError("residual enumeration incomplete")
    if record["before"]["active"] > len(residuals):
        raise ValueError("residual membership changed")
    if record["result"] not in ("natural", "telemetry_cleanup"):
        raise ValueError("invalid result")
    forced = record["result"] == "telemetry_cleanup"
    if type(record["forced_cleanup"]) is not bool or record["forced_cleanup"] != forced or bool(residuals) != forced:
        raise ValueError("cleanup mismatch")
    seen = set()
    for item in residuals:
        _keys(item, ("pid", "creation_time", "path", "sha256", "size", "version", "file_id", "member", "action", "signaled", "closed"))
        _number(item["pid"], 1)
        _number(item["creation_time"], 1)
        if (item["pid"], item["creation_time"]) == (record["root"]["pid"], record["root"]["creation_time"]):
            raise ValueError("residual is exited root")
        _number(item["size"], 1)
        if item["pid"] in seen or any(item[key] != value for key, value in pin.items()):
            raise ValueError("residual identity mismatch")
        seen.add(item["pid"])
        if item["member"] is not True or item["action"] not in ("terminate", "already_exited") or item["signaled"] is not True or item["closed"] is not True:
            raise ValueError("residual cleanup incomplete")


def _validate_outputs(outputs, paths):
    if not isinstance(outputs, list) or len(outputs) != len(paths):
        raise ValueError("output count mismatch")
    for item, path in zip(outputs, paths):
        _keys(item, ("path", "sha256", "size", "fresh_before_launch"))
        _sha(item["sha256"])
        _number(item["size"], 1)
        if item["path"] != path or item["fresh_before_launch"] is not True:
            raise ValueError("output identity or freshness mismatch")
