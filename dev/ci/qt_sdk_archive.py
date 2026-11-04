"""Backend-independent validation and staging for the pinned Linux Qt SDK.

The archive reader must provide Entry records and a fresh binary stream for
each regular file. It must reject hard links and undecodable names, preserve
the entry type and permissions, and surface reader errors. The caller must
verify every archive before reading any entry. This module deliberately does
not select or invoke an archive reader.
"""

from dataclasses import dataclass
import ctypes
import hashlib
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import sys
import urllib.parse
import urllib.request


@dataclass(frozen=True)
class Entry:
    path: str
    kind: str  # file, directory, symlink; all other types are rejected
    size: int = 0
    mode: int = 0o644
    link: str = ""
    open_data: object = None  # callable returning a fresh binary context manager


def relative(value):
    if (not isinstance(value, str) or not value or "\\" in value or
            any(part in ("", ".", "..") or ":" in part for part in value.split("/")) or
            value.startswith("/") or "\x00" in value):
        raise ValueError(f"unsafe relative path: {value!r}")
    return PurePosixPath(value)


def validate_link(path, target, root):
    if (not isinstance(target, str) or not target or target.startswith("/") or
            "\\" in target or ":" in target or "\x00" in target):
        raise ValueError(f"unsafe symlink target: {target!r}")
    parts = list(path.parent.parts)
    for part in target.split("/"):
        if part in ("", "."):
            continue
        if part == "..":
            if not parts:
                raise ValueError(f"escaping symlink target: {target!r}")
            parts.pop()
        else:
            parts.append(part)
    if tuple(parts[:len(root.parts)]) != root.parts:
        raise ValueError(f"symlink escapes SDK prefix: {target!r}")


def validate_link_graph(planned, root):
    links = {}
    for path, entry in planned:
        if entry.kind == "symlink":
            if path in links and links[path] != entry.link:
                raise ValueError(f"conflicting symlink overlay: {path}")
            links[path] = entry.link
    for path, target in links.items():
        parts, pending, hops = list(path.parent.parts), target.split("/"), 0
        while pending:
            part = pending.pop(0)
            if part in ("", "."):
                continue
            if part == "..":
                if len(parts) <= len(root.parts):
                    raise ValueError(f"symlink graph escapes SDK prefix: {path}")
                parts.pop()
                continue
            parts.append(part)
            linked = links.get(PurePosixPath(*parts))
            if linked is not None:
                hops += 1
                if hops > len(links):
                    raise ValueError(f"cyclic symlink graph: {path}")
                parts.pop()
                pending = linked.split("/") + pending


def validate_manifest(platform):
    target = relative(platform["target_subdirectory"])
    entries = [a for group in platform["groups"] for a in group["archives"]]
    if len(entries) != 10 or len({a["url"] for a in entries}) != 10:
        raise ValueError("expected ten unique Linux archives")
    for item in entries:
        url = urllib.parse.urlsplit(item["url"])
        destination = relative(item["extract_subdirectory"])
        if (url.scheme != "https" or url.netloc != "download.qt.io" or
                url.query or url.fragment or not url.path.endswith(".7z") or
                target not in (destination, *destination.parents) or
                type(item["bytes"]) is not int or item["bytes"] <= 0 or
                not re.fullmatch(r"[0-9a-f]{64}", item["sha256"])):
            raise ValueError("invalid pinned archive metadata")
    if sum(a["bytes"] for a in entries) != platform["archive_bytes_total"]:
        raise ValueError("archive total mismatch")
    estimate = sum(g["expanded_bytes_metadata"] for g in platform["groups"])
    if estimate <= 0 or estimate != platform["expanded_bytes_metadata_total"]:
        raise ValueError("expansion estimate mismatch")
    return entries


def download_archive(item, destination):
    digest, total = hashlib.sha256(), 0
    with urllib.request.urlopen(item["url"], timeout=120) as response, Path(destination).open("xb") as output:
        while chunk := response.read(1024 * 1024):
            total += len(chunk)
            if total > item["bytes"]:
                raise ValueError("archive exceeds pinned byte count")
            digest.update(chunk)
            output.write(chunk)
    if total != item["bytes"] or digest.hexdigest() != item["sha256"]:
        raise ValueError("archive byte count or SHA-256 mismatch")


def verify_archive(item, path):
    digest, total = hashlib.sha256(), 0
    with Path(path).open("rb") as source:
        while chunk := source.read(1024 * 1024):
            total += len(chunk)
            if total > item["bytes"]:
                raise ValueError("archive exceeds pinned byte count")
            digest.update(chunk)
    if total != item["bytes"] or digest.hexdigest() != item["sha256"]:
        raise ValueError("archive byte count or SHA-256 mismatch")


def check_storage(path, archive_bytes, expansion_limit, build_reserve):
    if any(type(n) is not int or n < 0 for n in (archive_bytes, expansion_limit, build_reserve)):
        raise ValueError("invalid storage accounting")
    required = archive_bytes + expansion_limit + build_reserve
    if shutil.disk_usage(path).free < required:
        raise ValueError(f"insufficient free space: need {required} bytes")
    return required


def plan_entries(groups, expansion_limit, sdk_root):
    """Validate all headers before writing. groups are (destination, entries)."""
    if type(expansion_limit) is not int or expansion_limit <= 0:
        raise ValueError("invalid expansion limit")
    root = relative(sdk_root)
    if root.parts[0].startswith(".qt-entry-"):
        raise ValueError("reserved staging path")
    planned, kinds, modes, total = [], {}, {}, 0
    for destination, entries in groups:
        base = relative(destination)
        if root not in (base, *base.parents):
            raise ValueError("archive destination escapes SDK prefix")
        for entry in entries:
            path = base / relative(entry.path)
            if entry.kind not in ("file", "directory", "symlink"):
                raise ValueError(f"unsupported archive entry: {path}")
            if type(entry.mode) is not int or entry.mode < 0 or entry.mode & ~0o777:
                raise ValueError(f"unsupported permissions: {path}")
            if type(entry.size) is not int or entry.size < 0:
                raise ValueError(f"invalid entry size: {path}")
            if entry.kind == "file":
                if not callable(entry.open_data):
                    raise ValueError(f"missing file reader: {path}")
                total += entry.size
                if total > expansion_limit:
                    raise ValueError("listed expansion exceeds configured limit")
            elif entry.kind == "symlink":
                if entry.mode != 0o777:
                    raise ValueError(f"unsupported symlink permissions: {path}")
                validate_link(path, entry.link, root)
            previous = kinds.setdefault(path, entry.kind)
            if previous != entry.kind:
                raise ValueError(f"conflicting entry types: {path}")
            if entry.kind in ("directory", "symlink"):
                old_mode = modes.setdefault(path, entry.mode)
                if old_mode != entry.mode:
                    raise ValueError(f"conflicting overlay permissions: {path}")
            planned.append((path, entry))
    for path in kinds:
        if any(kinds.get(parent) not in (None, "directory") for parent in path.parents):
            raise ValueError(f"entry traverses non-directory parent: {path}")
    validate_link_graph(planned, root)
    return planned


def copy_entry(entry, target, remaining):
    digest, total = hashlib.sha256(), 0
    with entry.open_data() as source, target.open("xb") as output:
        while chunk := source.read(min(1024 * 1024, remaining - total + 1)):
            total += len(chunk)
            if total > entry.size or total > remaining:
                raise ValueError("actual expansion exceeds listed size or limit")
            digest.update(chunk)
            output.write(chunk)
    if total != entry.size:
        raise ValueError("actual expansion does not match listed size")
    target.chmod(entry.mode)
    return total, digest.digest()


def publish_stage(stage, published):
    """Atomically publish without replacing an existing destination."""
    if sys.platform == "win32":
        os.rename(stage, published)
    elif sys.platform.startswith("linux"):
        libc = ctypes.CDLL(None, use_errno=True)
        try:
            rename = libc.renameat2
        except AttributeError as error:
            raise RuntimeError("atomic no-replace publication unavailable") from error
        rename.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
        rename.restype = ctypes.c_int
        if rename(-100, os.fsencode(stage), -100, os.fsencode(published), 1) != 0:
            code = ctypes.get_errno()
            raise OSError(code, os.strerror(code), str(published))
    else:
        raise RuntimeError("atomic no-replace publication unavailable on this platform")


def stage_entries(groups, stage, published, expansion_limit, sdk_root):
    """Create a fresh stage and publish only after all entries are validated."""
    planned = plan_entries(groups, expansion_limit, sdk_root)
    stage, published = Path(stage), Path(published)
    if stage.parent.resolve() != published.parent.resolve() or published.exists() or published.is_symlink():
        raise ValueError("publication requires an absent sibling destination")
    stage.mkdir(exist_ok=False)
    seen, actual = {}, 0
    for index, (path, entry) in enumerate(planned):
        target = stage.joinpath(*path.parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        if entry.kind == "directory":
            target.mkdir(exist_ok=True)
            seen[path] = ("directory", entry.mode)
        elif entry.kind == "symlink":
            prior = seen.get(path)
            if prior is not None and prior != ("symlink", entry.mode, entry.link):
                raise ValueError(f"conflicting overlay: {path}")
            seen[path] = ("symlink", entry.mode, entry.link)
        else:
            temporary = stage / f".qt-entry-{index}"
            count, digest = copy_entry(entry, temporary, expansion_limit - actual)
            actual += count
            identity = ("file", entry.mode, count, digest)
            if path in seen:
                if seen[path] != identity:
                    raise ValueError(f"conflicting overlay: {path}")
                temporary.unlink()
            else:
                temporary.rename(target)
                seen[path] = identity
    for path, identity in seen.items():
        if identity[0] == "symlink":
            os.symlink(identity[2], stage.joinpath(*path.parts))
    for path, identity in sorted(seen.items(), key=lambda item: len(item[0].parts), reverse=True):
        if identity[0] == "directory":
            stage.joinpath(*path.parts).chmod(identity[1])
    publish_stage(stage, published)
    return published.resolve()
