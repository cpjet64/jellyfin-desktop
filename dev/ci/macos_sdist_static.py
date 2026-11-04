"""Acquire pinned sdists and inspect metadata without extracting or executing them."""

import argparse
import ctypes
import errno
import gzip
import hashlib
import json
import os
import shutil
import stat
import time
from pathlib import Path, PurePosixPath
import re
import urllib.request

TRANSFER_LIMIT = 8 * 1024 * 1024
EXPANSION_LIMIT = 256 * 1024 * 1024
WORKSPACE_LIMIT = 512 * 1024 * 1024
RESERVE_BYTES = 480 * 1024 * 1024
METADATA_LIMIT = 1024 * 1024
RESULT_LIMIT = 16 * 1024 * 1024
MEMBER_LIMIT = 100000
PAX_LIMIT = 65536
DEADLINE_SECONDS = 120
MANIFEST = Path(__file__).with_name("macos_sdist_manifest.json")
REPO = Path(__file__).resolve().parents[2]


def load_manifest():
    entries = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if len(entries) != 6 or sum(e["size"] for e in entries) != 5421756:
        raise ValueError("unexpected manifest size or count")
    for entry in entries:
        if not re.fullmatch(r"[a-zA-Z0-9_.-]+\.tar\.gz", entry["name"]):
            raise ValueError("invalid archive name")
        if not re.fullmatch(r"[0-9a-f]{64}", entry["sha256"]):
            raise ValueError("invalid digest")
        if not re.fullmatch(r"https://files\.pythonhosted\.org/packages/[a-zA-Z0-9/_.-]+", entry["url"]):
            raise ValueError("invalid source URL")
        if entry["size"] <= 0 or entry["size"] > TRANSFER_LIMIT:
            raise ValueError("invalid source size")
    if len({e["name"] for e in entries}) != len(entries):
        raise ValueError("duplicate archive name")
    return entries


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        raise ValueError("redirect refused")


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


def acquire(entry, directory, transferred):
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
    digest = hashlib.sha256()
    count = 0
    destination = directory / entry["name"]
    deadline = time.monotonic() + DEADLINE_SECONDS
    with opener.open(entry["url"], timeout=5) as response, destination.open("xb") as out:
        while count < entry["size"]:
            if time.monotonic() >= deadline:
                raise TimeoutError("acquisition deadline exceeded")
            remaining = min(entry["size"] - count, TRANSFER_LIMIT - transferred - count)
            if remaining <= 0:
                raise ValueError("transfer cap exceeded")
            chunk = response.read(min(65536, remaining))
            if not chunk:
                break
            count += len(chunk)
            out.write(chunk)
            digest.update(chunk)
        if transferred + count >= TRANSFER_LIMIT:
            raise ValueError("no transfer budget for extra-byte check")
        if time.monotonic() >= deadline:
            raise TimeoutError("acquisition deadline exceeded")
        if response.read(1):
            raise ValueError("unexpected extra source data")
    if count != entry["size"] or digest.hexdigest() != entry["sha256"]:
        raise ValueError(f"identity mismatch: {entry['name']}")
    return transferred + count


def safe_member(name, kind):
    path = PurePosixPath(name)
    if (not name or "\\" in name or name.startswith("/") or
            any(part in ("..", ".") for part in name.split("/")) or
            re.match(r"^[A-Za-z]:", name) or str(path) != name.rstrip("/") or
            kind not in (b"0", b"\0", b"5")):
        raise ValueError(f"unsafe archive member: {name!r}")
    return path


class CountedReader:
    def __init__(self, stream, total):
        self.stream = stream
        self.total = total

    def read(self, size=-1):
        remaining = EXPANSION_LIMIT - self.total
        if remaining <= 0:
            raise ValueError("expanded size cap reached")
        chunk = self.stream.read(min(size if size >= 0 else remaining, remaining))
        self.total += len(chunk)
        return chunk


def candidate(path):
    return (path.name in ("pyproject.toml", "PKG-INFO", "setup.cfg", "setup.py", "meson.build", "meson.options", "meson_options.txt")
            or "subprojects" in path.parts and path.suffix in (".wrap", ".patch", ".toml", ".txt", ".build")
            or any(part in ("mesonpy", "flit_core") for part in path.parts) and path.suffix == ".py")


def inspect_archive(path, expanded, result_size=0):
    found = {}
    inventory = []
    with gzip.open(path, "rb") as compressed:
        reader = CountedReader(compressed, expanded)
        return inspect_stream(reader, found, inventory, result_size)


def inspect_stream(reader, found, inventory, result_size):
    pending = None
    seen = set()
    while True:
        header = read_exact(reader, 512)
        if header == bytes(512):
            if pending is not None:
                raise ValueError("orphan PAX header")
            if read_exact(reader, 512) != bytes(512):
                raise ValueError("missing tar end marker")
            break
        name, kind, size = parse_header(header)
        if kind == b"x":
            if pending is not None or size > PAX_LIMIT:
                raise ValueError("duplicate or oversized PAX header")
            pending = parse_pax(read_payload(reader, size, True))
            read_exact(reader, (-size) % 512)
            continue
        if pending is not None:
            name = pending.get("path", name)
            size = int(pending.get("size", size))
            pending = None
        path = safe_member(name, kind)
        if str(path) in seen:
            raise ValueError("duplicate archive member")
        seen.add(str(path))
        inventory.append(name)
        result_size += len(name.encode("utf-8")) + 8
        if len(inventory) > MEMBER_LIMIT or result_size > RESULT_LIMIT:
            raise ValueError("inventory/result cap exceeded")
        if kind == b"5" and size:
            raise ValueError("directory has payload")
        selected = kind != b"5" and candidate(path)
        if selected and (size > METADATA_LIMIT or result_size + size * 6 > RESULT_LIMIT):
            raise ValueError("metadata/result cap exceeded")
        data = read_payload(reader, size, selected)
        if selected:
            if name in found:
                raise ValueError("duplicate metadata")
            found[name] = data.decode("utf-8", errors="strict")
            result_size += len(data) * 6
        read_exact(reader, (-size) % 512)
    while True:
        tail = reader.read(65536)
        if not tail:
            break
        if any(tail):
            raise ValueError("nonzero data after tar end marker")
    return reader.total, {"inventory": inventory, "text": found}, result_size


def parse_pax(data):
    result = {}
    offset = 0
    while offset < len(data):
        space = data.find(b" ", offset, min(len(data), offset + 12))
        if space < 0:
            raise ValueError("invalid PAX record length")
        length_bytes = data[offset:space]
        if not length_bytes.isdigit() or length_bytes.startswith(b"0"):
            raise ValueError("invalid PAX record length")
        end = offset + int(length_bytes)
        if end > len(data) or end <= space + 2 or data[end - 1:end] != b"\n":
            raise ValueError("truncated PAX record")
        try:
            key, value = data[space + 1:end - 1].decode("utf-8").split("=", 1)
        except (ValueError, UnicodeError) as error:
            raise ValueError("invalid PAX record") from error
        if key in result or key not in ("path", "size", "mtime") or not value or "\0" in value:
            raise ValueError("unsupported or duplicate PAX key")
        if key == "size" and (not value.isascii() or not value.isdigit() or int(value) > EXPANSION_LIMIT):
            raise ValueError("invalid PAX size")
        if key == "mtime" and not re.fullmatch(r"-?\d+(?:\.\d+)?", value):
            raise ValueError("invalid PAX mtime")
        result[key] = value
        offset = end
    return result


def read_exact(reader, size):
    pieces = []
    while size:
        part = reader.read(min(size, 65536))
        if not part:
            raise ValueError("truncated archive")
        pieces.append(part)
        size -= len(part)
    return b"".join(pieces)


def read_payload(reader, size, retain):
    parts = []
    while size:
        chunk = read_exact(reader, min(size, 65536))
        if retain:
            parts.append(chunk)
        size -= len(chunk)
    return b"".join(parts)


def parse_header(header):
    if header[257:263] not in (b"ustar\0", b"ustar "):
        raise ValueError("unsupported tar format")
    try:
        checksum = int(header[148:156].strip(b" \0"), 8)
        size = int(header[124:136].strip(b" \0") or b"0", 8)
        name = header[:100].split(b"\0", 1)[0].decode("utf-8")
        prefix = header[345:500].split(b"\0", 1)[0].decode("utf-8")
    except (ValueError, UnicodeError) as error:
        raise ValueError("invalid tar header") from error
    if checksum != sum(header[:148]) + 8 * 32 + sum(header[156:]) or not 0 <= size <= EXPANSION_LIMIT:
        raise ValueError("invalid tar checksum or size")
    return (prefix + "/" if prefix else "") + name, header[156:157], size


def validate_directory(value):
    directory = Path(value).absolute()
    parent = REPO / "deps" / "w463-sdist-proof"
    if directory.parent != parent or directory.exists() or directory.is_symlink():
        raise ValueError("destination must be a new child of deps/w463-sdist-proof")
    for part in (REPO, REPO / "deps", parent):
        info = part.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError("reparse point in destination ancestry")
    if shutil.disk_usage(parent).free < WORKSPACE_LIMIT:
        raise ValueError("insufficient free space for 512 MiB admission")
    return directory


def allocated_size(path):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    query = kernel.GetCompressedFileSizeW
    query.argtypes = (ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_ulong))
    query.restype = ctypes.c_ulong
    high = ctypes.c_ulong()
    ctypes.set_last_error(0)
    low = query(str(path), ctypes.byref(high))
    error = ctypes.get_last_error()
    if low == 0xffffffff and error:
        raise OSError(error, "reservation allocation query failed")
    return (high.value << 32) | low


def reserve(directory):
    path = directory / "reservation.bin"
    block = bytes(1024 * 1024)
    with path.open("xb") as stream:
        for _ in range(RESERVE_BYTES // len(block)):
            stream.write(block)
        stream.flush()
        os.fsync(stream.fileno())
    if path.stat().st_size != RESERVE_BYTES or allocated_size(path) < RESERVE_BYTES:
        raise OSError("reservation size mismatch")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--acquire-and-inspect", required=True, metavar="NEW_DIRECTORY")
    args = parser.parse_args()
    entries = load_manifest()
    directory = validate_directory(args.acquire_and_inspect)
    directory.mkdir()
    reserve(directory)
    transferred = expanded = result_size = 0
    results = {}
    for entry in entries:
        transferred = acquire(entry, directory, transferred)
        if RESERVE_BYTES + transferred > WORKSPACE_LIMIT:
            raise ValueError("workspace cap exceeded")
        expanded, metadata, result_size = inspect_archive(directory / entry["name"], expanded, result_size)
        if RESERVE_BYTES + transferred + result_size > WORKSPACE_LIMIT:
            raise ValueError("workspace/output cap exceeded")
        results[entry["name"]] = metadata
    output = json.dumps({"transferred": transferred, "expanded": expanded, "metadata": results}, indent=2)
    if len(output.encode("utf-8")) > RESULT_LIMIT:
        raise ValueError("serialized result cap exceeded")
    print(output)


if __name__ == "__main__":
    try:
        main()
    except BaseException as error:
        if storage_failure(error):
            os._exit(75)
        raise
