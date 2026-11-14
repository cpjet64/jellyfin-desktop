"""Review-bound evidence contract for the unexecuted offline producer."""

import hashlib
import json
from pathlib import Path
import re

HEX = re.compile(r"[0-9a-f]{64}\Z")
KINDS = ("provider_graph", "provider_overlays", "nested_cargo", "rust_toolchain",
         "input_lock", "reviewed_modules")


def object_digest(value):
    encoded = json.dumps(value, sort_keys=True, separators=(",", ":"),
                         ensure_ascii=True, allow_nan=False).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


def expected_bindings(lock, lock_sha, inventory_sha, review):
    """Derive subjects from actual approved inputs, never readiness booleans."""
    if not HEX.fullmatch(lock_sha) or not HEX.fullmatch(inventory_sha):
        raise ValueError("invalid input lock or inventory identity")
    return {
        "provider_graph": object_digest({"provider": lock["provider_commit"],
                                         "mpv": lock["mpv_commit"], "sources": lock["sources"]}),
        "provider_overlays": object_digest(lock["provider_adapter"]),
        "nested_cargo": object_digest(lock["cargo_inventories"]),
        "rust_toolchain": object_digest({"manifest": lock["rust_manifest_sha256"],
                                         "components": lock["rust_components"],
                                         "host_tools": lock["host_tools"]}),
        "input_lock": object_digest({"lock_sha256": lock_sha, "inventory_sha256": inventory_sha}),
        "reviewed_modules": object_digest({"source_sha": review["reviewed_source_sha"],
                                           "files": review["files"]}),
    }


def validate_evidence(root, lock, lock_sha, inventory_sha, review, evidence):
    """Bind exactly six qualified review records to their checked proof files.

    Qualification is a review assertion, not a conclusion derived from a
    checksum. Proof files and this evidence document must be reviewed and bound
    by the independently approved manifest before a producer can be run.
    """
    if not isinstance(evidence, dict) or set(evidence) != {"schema_version", "records"}:
        raise ValueError("invalid evidence document")
    if evidence["schema_version"] != 1 or not isinstance(evidence["records"], list):
        raise ValueError("unsupported evidence schema")
    expected = expected_bindings(lock, lock_sha, inventory_sha, review)
    seen = set()
    for record in evidence["records"]:
        if not isinstance(record, dict) or set(record) != {"kind", "status", "subject_sha256", "proof_path", "proof_sha256"}:
            raise ValueError("invalid evidence record")
        kind = record["kind"]
        if kind not in KINDS or kind in seen or record["status"] != "qualified":
            raise ValueError("missing, duplicate, or unqualified evidence")
        if record["subject_sha256"] != expected[kind]:
            raise ValueError("evidence subject differs from approved inputs: " + kind)
        relative = record["proof_path"]
        if not isinstance(relative, str) or not relative or "\\" in relative:
            raise ValueError("invalid evidence proof path")
        parts = relative.split("/")
        if any(part in ("", ".", "..") or ":" in part for part in parts):
            raise ValueError("invalid evidence proof path")
        proof = Path(root)
        for part in parts:
            proof = proof / part
            if proof.is_symlink():
                raise ValueError("linked evidence proof")
        claimed = record["proof_sha256"]
        if not isinstance(claimed, str) or not HEX.fullmatch(claimed):
            raise ValueError("invalid proof identity")
        if review["files"].get(relative) != claimed or hashlib.sha256(proof.read_bytes()).hexdigest() != claimed:
            raise ValueError("proof file not bound to reviewed manifest")
        seen.add(kind)
    if seen != set(KINDS):
        raise ValueError("required evidence record missing")
