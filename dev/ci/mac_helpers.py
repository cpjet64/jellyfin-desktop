"""Fail-closed primitives for offline qualification of pinned macOS helpers.

This module does not download or execute helpers. Its callers must enforce the
aggregate acquisition and workspace budgets and serialize the qualification.
"""
import hashlib
import errno
import os
from pathlib import Path
import stat
import zipfile

CHUNK = 1024 * 1024


def storage_exhausted(exc):
    """Recognize direct or wrapped storage failures without following cycles."""
    seen = set()
    while exc is not None and id(exc) not in seen:
        seen.add(id(exc))
        if isinstance(exc, OSError) and exc.errno in (errno.ENOSPC, getattr(errno, "EDQUOT", -1)):
            return True
        exc = getattr(exc, "reason", None) or exc.__cause__ or exc.__context__
    return False


def checked_copy(source, destination, size, sha256):
    """Stream into a private staging file; this does not publish atomically."""
    digest = hashlib.sha256()
    count = 0
    try:
        with open(destination, "xb") as output:
            while True:
                chunk = source.read(min(CHUNK, size - count + 1))
                if not chunk:
                    break
                count += len(chunk)
                if count > size:
                    raise ValueError("input exceeds approved size")
                digest.update(chunk)
                output.write(chunk)
            if count != size or digest.hexdigest() != sha256:
                raise ValueError("size or SHA-256 mismatch")
    except BaseException as exc:
        # Only remove a file created by this invocation, never an existing file.
        storage_failure = storage_exhausted(exc)
        if 'output' in locals() and not storage_failure:
            os.unlink(destination)
        raise


def extract_deno(archive, stage, expected_sha256, max_expanded):
    """Extract the sole regular 'deno' member to a new, private stage."""
    stage = Path(stage)
    stage.mkdir(mode=0o700)  # exclusive; parents must already exist
    destination = stage / "deno"
    try:
        with zipfile.ZipFile(archive) as zf:
            entries = zf.infolist()
            if len(entries) != 1:
                raise ValueError("expected exactly one ZIP member")
            entry = entries[0]
            mode = entry.external_attr >> 16
            kind = stat.S_IFMT(mode)
            if (entry.filename != "deno" or entry.is_dir() or
                    kind not in (0, stat.S_IFREG) or entry.flag_bits & 1):
                raise ValueError("unexpected, special, or encrypted ZIP member")
            if entry.file_size > max_expanded:
                raise ValueError("expanded payload exceeds limit")
            with zf.open(entry) as source:
                checked_copy(source, destination, entry.file_size, expected_sha256)
    except BaseException as exc:
        storage_failure = storage_exhausted(exc)
        if not storage_failure:
            stage.rmdir()  # checked_copy removes its own partial file
        raise
    return destination


def cli_arguments(yt_dlp, deno):
    """Controlled direct CLI selection; no shell quoting is required."""
    if not Path(yt_dlp).is_absolute() or not Path(deno).is_absolute():
        raise ValueError("helper paths must be absolute")
    return [str(yt_dlp), "--ignore-config", "--no-plugin-dirs",
            "--no-update", "--no-remote-components", "--no-js-runtimes",
            "--js-runtimes", f"deno:{deno}"]
