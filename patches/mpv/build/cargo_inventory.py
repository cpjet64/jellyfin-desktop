"""Record pinned Cargo source identities without fetching or invoking Cargo."""

import argparse
import hashlib
import json
import re
import tomllib
from pathlib import Path

EXPECTED = "03cef7de9f939c8e24ebada9bdc6d4efbe1a77055f98a96618f3d73b4d2840a4"
CHECKSUM = re.compile(r"^[0-9a-f]{64}$")


def inventory(path):
    payload = path.read_bytes()
    actual = hashlib.sha256(payload).hexdigest()
    if actual != EXPECTED:
        raise ValueError("Cargo.lock SHA-256 differs from selected source")
    document = tomllib.loads(payload.decode("utf-8"))
    if document.get("version") != 4:
        raise ValueError("unexpected Cargo.lock format")
    records, identities = [], set()
    for item in document["package"]:
        source = item.get("source")
        key = (item["name"], item["version"], source)
        if key in identities:
            raise ValueError(f"duplicate package identity: {key}")
        identities.add(key)
        if source and not source.startswith("registry+"):
            raise ValueError(f"unresolved non-registry Cargo source: {key}")
        checksum = item.get("checksum")
        if source and (not checksum or not CHECKSUM.fullmatch(checksum)):
            raise ValueError(f"missing registry checksum: {key}")
        records.append({"name": item["name"], "version": item["version"],
                        "source": source, "checksum": checksum,
                        "dependencies": item.get("dependencies", [])})
    return {"status": "Cargo source identities only; offline availability and build not verified",
            "source_commit": "fe13c58e178431ec469c556a91bc080096738dc7",
            "cargo_lock_sha256": actual, "packages": records}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("lock", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = inventory(args.lock)
    with args.output.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(result, stream, indent=2, ensure_ascii=True)
        stream.write("\n")


if __name__ == "__main__":
    main()
