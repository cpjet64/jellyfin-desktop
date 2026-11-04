"""Copy an explicitly supplied complete Qt archive set into a fresh job."""

import hashlib
import json
import os
from pathlib import Path
import stat


EVIDENCE = "qt-archive-input.json"


def validate_mapping(items, mapping):
    expected = {item["url"] for item in items}
    if (not isinstance(mapping, dict) or len(items) != 10 or len(expected) != 10 or
            set(mapping) != expected):
        raise ValueError("cached archive mapping must match all ten pinned URLs")
    paths = []
    for item in items:
        raw = mapping[item["url"]]
        if not isinstance(raw, str) or not raw:
            raise ValueError("cached archive path must be an absolute string")
        path = Path(raw)
        if not path.is_absolute() or path.resolve(strict=True) != path:
            raise ValueError("cached archive path must be canonical and not a symlink")
        info = path.lstat()
        if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1:
            raise ValueError("cached archive must be an unaliased regular file")
        paths.append(path)
    if len(set(paths)) != len(paths):
        raise ValueError("cached archive paths must be distinct")
    return paths


def copy_one(source, destination, item):
    original = source.lstat()
    read_flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    source_fd = os.open(source, read_flags)
    try:
        info = os.fstat(source_fd)
        if (not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or
                (info.st_dev, info.st_ino) != (original.st_dev, original.st_ino) or
                info.st_size != item["bytes"]):
            raise ValueError("cached archive identity or pinned size mismatch")
        flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
        output_fd = os.open(destination, flags, 0o600)
        with os.fdopen(output_fd, "wb") as output:
            created = os.fstat(output.fileno())
            if (created.st_dev, created.st_ino) == (info.st_dev, info.st_ino) or created.st_nlink != 1:
                raise ValueError("archive copy is not independent")
            digest, count = hashlib.sha256(), 0
            while chunk := os.read(source_fd, min(1024 * 1024, item["bytes"] - count + 1)):
                count += len(chunk)
                if count > item["bytes"]:
                    raise ValueError("cached archive exceeds pinned size")
                digest.update(chunk)
                output.write(chunk)
            output.flush()
            os.fsync(output.fileno())
        final = os.fstat(source_fd)
        if (count != item["bytes"] or digest.hexdigest() != item["sha256"] or
                (final.st_dev, final.st_ino, final.st_size, final.st_mtime_ns) !=
                (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns)):
            raise ValueError("cached archive bytes or identity changed")
    finally:
        os.close(source_fd)
    return verify_copy(destination, item)


def verify_copy(path, item):
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_size != item["bytes"]:
        raise ValueError("copied archive type or size mismatch")
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    fd = os.open(path, flags)
    try:
        opened = os.fstat(fd)
        if (not stat.S_ISREG(opened.st_mode) or opened.st_nlink != 1 or opened.st_size != item["bytes"] or
                (opened.st_dev, opened.st_ino) != (info.st_dev, info.st_ino)):
            raise ValueError("copied archive changed before verification")
        digest, count = hashlib.sha256(), 0
        while chunk := os.read(fd, min(1024 * 1024, item["bytes"] - count + 1)):
            count += len(chunk)
            if count > item["bytes"]:
                raise ValueError("copied archive exceeds pinned size")
            digest.update(chunk)
        final = os.fstat(fd)
        if (count != item["bytes"] or digest.hexdigest() != item["sha256"] or
                (final.st_dev, final.st_ino, final.st_size, final.st_mtime_ns) !=
                (opened.st_dev, opened.st_ino, opened.st_size, opened.st_mtime_ns)):
            raise ValueError("copied archive SHA-256 mismatch")
        return digest.hexdigest()
    finally:
        os.close(fd)


def copy_archives(items, mapping, workspace, manifest_sha256):
    sources = validate_mapping(items, mapping)
    workspace = Path(workspace)
    if (not workspace.is_absolute() or workspace.resolve(strict=True) != workspace or not workspace.is_dir() or
            (hasattr(os, "getuid") and workspace.stat().st_uid != os.getuid())):
        raise ValueError("cache destination workspace must be canonical and job-owned")
    directory = workspace / "qt-archives"
    directory.mkdir(mode=0o700, exist_ok=False)
    archives, records = [], []
    for index, (item, source) in enumerate(zip(items, sources, strict=True)):
        target = directory / f"{index:02d}.7z"
        actual = copy_one(source, target, item)
        archives.append((item, target))
        records.append({"url": item["url"], "file": target.name, "bytes": item["bytes"], "sha256": actual})
    evidence = {"source": "explicit-cache", "manifest_sha256": manifest_sha256, "archives": records}
    payload = json.dumps(evidence, sort_keys=True).encode("utf-8") + b"\n"
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    with os.fdopen(os.open(workspace / EVIDENCE, flags, 0o600), "wb") as output:
        output.write(payload)
        output.flush()
        os.fsync(output.fileno())
    return archives


def verify_evidence(workspace, items, manifest_sha256):
    path = Path(workspace) / EVIDENCE
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_size > 65536:
        raise ValueError("cached archive evidence is invalid")
    with os.fdopen(os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)), "rb") as source:
        opened = os.fstat(source.fileno())
        if (not stat.S_ISREG(opened.st_mode) or opened.st_nlink != 1 or
                (opened.st_dev, opened.st_ino) != (info.st_dev, info.st_ino)):
            raise ValueError("cached archive evidence changed")
        payload = source.read(65537)
    evidence = json.loads(payload)
    if not isinstance(evidence, dict):
        raise ValueError("cached archive evidence is invalid")
    records = evidence.get("archives")
    if (set(evidence) != {"source", "manifest_sha256", "archives"} or
            len(payload) != info.st_size or len(payload) > 65536 or evidence.get("source") != "explicit-cache" or
            evidence.get("manifest_sha256") != manifest_sha256 or not isinstance(records, list) or
            len(records) != len(items) or len(items) != 10):
        raise ValueError("cached archive evidence does not match manifest")
    directory = Path(workspace) / "qt-archives"
    if not stat.S_ISDIR(directory.lstat().st_mode) or directory.resolve(strict=True) != directory:
        raise ValueError("cached archive copy directory changed")
    if {entry.name for entry in directory.iterdir()} != {f"{index:02d}.7z" for index in range(10)}:
        raise ValueError("cached archive copy set changed")
    for index, (item, record) in enumerate(zip(items, records, strict=True)):
        expected = {"url": item["url"], "file": f"{index:02d}.7z", "bytes": item["bytes"],
                    "sha256": item["sha256"]}
        if record != expected or verify_copy(directory / expected["file"], item) != expected["sha256"]:
            raise ValueError("cached archive copy or record changed")
    return {"source": "explicit-cache", "count": len(records), "manifest_sha256": manifest_sha256,
            "evidence_sha256": hashlib.sha256(payload).hexdigest(), "archives": records}
