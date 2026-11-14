"""Verify raw prestaged inputs for the proposed offline producer.

This is only an input-byte gate. It does not extract, configure, build, or
prove that a Git bundle contains the declared commit or nested source graph.
"""

import argparse
import hashlib
import re
import sys
from pathlib import Path

from recovery_lock_preflight import BUILD, RUST_COMPONENTS, parse_object, verify

SHA256 = re.compile(r"[0-9a-f]{64}\Z")


def file_hash(path, algorithm="sha256"):
    digest = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def plain_file(root, name):
    if not isinstance(name, str) or not name or "\\" in name or ":" in name:
        raise ValueError("invalid input path")
    parts = name.split("/")
    if any(part in ("", ".", "..") for part in parts):
        raise ValueError("invalid input path")
    path = root
    for part in parts:
        path = path / part
        info = path.lstat()
        if path.is_symlink() or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError("linked input path")
    path.resolve(strict=True).relative_to(root.resolve(strict=True))
    if not path.is_file():
        raise ValueError("input is not a regular file")
    return path


def verify_materials(lock, root, inventory):
    payload = (BUILD / "subrandr-registry-inventory.json").read_bytes().replace(b"\r\n", b"\n")
    contract = lock.get("cargo_inventories", {}).get("subrandr", {})
    if hashlib.sha256(payload).hexdigest() != contract.get("inventory_sha256"):
        raise ValueError("Cargo inventory differs from source lock")
    registry = parse_object(payload)
    source = next((item for item in lock["sources"] if item["name"] == "subrandr"), {})
    if source.get("commit") != registry["source_commit"]:
        raise ValueError("Cargo inventory is for a different subrandr commit")
    if registry["cargo_lock_sha256"] != contract.get("cargo_lock_sha256"):
        raise ValueError("Cargo.lock identity differs from source lock")
    expected = {f"rust/{name}": value for name, value in RUST_COMPONENTS.items()}
    for package in registry["packages"]:
        if package.get("source") == "registry+https://github.com/rust-lang/crates.io-index":
            name = f"crate/{package['name']}/{package['version']}"
            if name in expected:
                raise ValueError(f"duplicate locked material: {name}")
            expected[name] = package["checksum"]
    records = inventory.get("materials")
    if not isinstance(records, list):
        raise ValueError("Rust and Cargo materials missing")
    found, paths = set(), set()
    for record in records:
        if not isinstance(record, dict) or set(record) != {"name", "path"}:
            raise ValueError("invalid material record")
        name, path = record["name"], record["path"]
        if name not in expected or name in found or not isinstance(path, str) or path in paths:
            raise ValueError("unknown or duplicate material")
        if file_hash(plain_file(root, path)) != expected[name]:
            raise ValueError(f"material differs from pinned checksum: {name}")
        found.add(name)
        paths.add(path)
    if found != set(expected):
        raise ValueError("Rust and Cargo material set is incomplete")


def verify_inventory(lock, root, inventory):
    current = root.absolute()
    while True:
        info = current.lstat()
        if current.is_symlink() or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError("linked input root")
        if current.parent == current:
            break
        current = current.parent
    if inventory.get("schema_version") != 1:
        raise ValueError("unsupported input inventory")
    expected = {item["name"]: item for item in lock["sources"]}
    records = inventory.get("sources")
    if not isinstance(records, list):
        raise ValueError("source inventory missing")
    names, paths = set(), set()
    for record in records:
        if not isinstance(record, dict) or set(record) != {"name", "path", "sha256", "size"}:
            raise ValueError("invalid source inventory record")
        name, path = record["name"], record["path"]
        if name not in expected or name in names or not isinstance(path, str) or path in paths:
            raise ValueError("unknown or duplicate source inventory entry")
        if not isinstance(record["size"], int) or record["size"] < 1:
            raise ValueError("invalid source size")
        if not isinstance(record["sha256"], str) or not SHA256.fullmatch(record["sha256"]):
            raise ValueError("invalid source digest")
        source = plain_file(root, path)
        if source.stat().st_size != record["size"] or file_hash(source) != record["sha256"]:
            raise ValueError(f"staged bytes changed: {name}")
        locked = expected[name]
        if locked["kind"] == "archive":
            for algorithm in ("sha256", "sha512"):
                if locked.get(algorithm) and file_hash(source, algorithm) != locked[algorithm]:
                    raise ValueError(f"archive differs from source lock: {name}")
        elif locked["kind"] == "git":
            raise ValueError(f"Git object and submodule verification is not implemented: {name}")
        else:
            raise ValueError(f"unsupported source kind: {name}")
        names.add(name)
        paths.add(path)
    if names != set(expected):
        raise ValueError("staged inventory does not cover the lock source list")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--lock", required=True, type=Path)
    parser.add_argument("--approved-sha256", required=True)
    parser.add_argument("--inventory", required=True, type=Path)
    parser.add_argument("--inventory-sha256", required=True)
    parser.add_argument("--inputs", required=True, type=Path)
    args = parser.parse_args()
    try:
        lock = verify(args.lock, args.approved_sha256)
        payload = args.inventory.read_bytes()
        if not SHA256.fullmatch(args.inventory_sha256) or hashlib.sha256(payload).hexdigest() != args.inventory_sha256:
            raise ValueError("inventory identity differs from approved digest")
        inventory = parse_object(payload)
        verify_materials(lock, args.inputs, inventory)
        verify_inventory(lock, args.inputs, inventory)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Offline input byte verification failed: {error}", file=sys.stderr)
        return 1
    print("Staged input bytes match the approved inventory; source identities still require verification.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
