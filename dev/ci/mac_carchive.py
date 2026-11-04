"""Exact-asset, inspection-only CArchive payload bound; never launches input."""
import hashlib
import os
from pathlib import Path
import stat
import time
import zlib

from mac_carchive_format import (COOKIE, FILE_TYPES, check_deadline, cookie_info,
                                 parse_entries, read_at, safe_name)
from mac_carchive_macho import validate_layout

ASSET_SIZE = 37466304
ASSET_SHA256 = '0f922c15ccc0b83eefa1c8bc9104e434318c4bf16e408e1ee93b7745a6efe0aa'
CHUNK = 65536
PAYLOAD_LIMIT = 512 * 1024 * 1024


def hash_file(source, deadline):
    source.seek(0)
    digest = hashlib.sha256()
    while True:
        check_deadline(deadline)
        chunk = source.read(CHUNK)
        if not chunk:
            return digest.hexdigest()
        digest.update(chunk)


def inflate_member(source, start, stored, limit, deadline):
    source.seek(start)
    inflater = zlib.decompressobj()
    remaining, total = stored, 0
    while remaining:
        check_deadline(deadline)
        data = source.read(min(CHUNK, remaining))
        if not data:
            raise ValueError('truncated compressed member')
        remaining -= len(data)
        while data:
            check_deadline(deadline)
            before = len(data)
            output = inflater.decompress(data, min(CHUNK, limit - total + 1))
            total += len(output)
            if total > limit or inflater.unused_data:
                raise ValueError('expanded limit or trailing compressed stream')
            data = inflater.unconsumed_tail
            if data and len(data) == before and not output:
                raise ValueError('decompressor made no progress')
            if inflater.eof and (data or remaining):
                raise ValueError('trailing compressed data')
    if not inflater.eof:
        raise ValueError('incomplete compressed stream')
    return total


def inspect_stream(source, size, digest, deadline):
    initial = os.fstat(source.fileno())
    if initial.st_size != size or hash_file(source, deadline) != digest:
        raise ValueError('asset identity mismatch')
    start, toc_off, toc_len, lib, _, cookie = cookie_info(source, size, deadline)
    validate_layout(source, size, start, cookie + COOKIE.size, deadline)
    entries, options = parse_entries(read_at(source, start + toc_off, toc_len, deadline), toc_off, deadline)
    library = safe_name(lib)
    if library not in [entry[0] for entry in entries if entry[5] == b'b']:
        raise ValueError('Python library is not a contained binary')
    file_total = 0
    nonfile_total = 0
    measured = []
    for name, offset, stored, expanded, flag, kind in entries:
        check_deadline(deadline)
        budget = PAYLOAD_LIMIT - file_total if kind in FILE_TYPES else PAYLOAD_LIMIT
        actual = inflate_member(source, start + offset, stored, budget, deadline) if flag else stored
        if actual != expanded or actual > budget:
            raise ValueError('member length or payload limit mismatch')
        if kind in FILE_TYPES:
            file_total += actual
        else:
            nonfile_total += actual
        measured.append((name, kind.decode('ascii'), actual))
    after = os.fstat(source.fileno())
    if (initial.st_dev, initial.st_ino, initial.st_size, initial.st_mtime_ns) != (
            after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns) or hash_file(source, deadline) != digest:
        raise ValueError('asset changed during inspection')
    return {'claim': 'CArchive payload expansion under recorded format interpretation',
            'file_bytes': file_total, 'nonfile_bytes': nonfile_total,
            'excluded': 'filesystem overhead, caches, evidence, workspace and runtime quota',
            'entries': measured, 'options': options}


def inspect_asset(path):
    deadline = time.monotonic() + 60
    path = Path(path)
    before = path.lstat()
    if not stat.S_ISREG(before.st_mode):
        raise ValueError('asset path must be a regular file')
    flags = os.O_RDONLY | getattr(os, 'O_BINARY', 0) | getattr(os, 'O_NOFOLLOW', 0) | getattr(os, 'O_NONBLOCK', 0)
    with os.fdopen(os.open(path, flags), 'rb') as source:
        opened = os.fstat(source.fileno())
        if not stat.S_ISREG(opened.st_mode) or (before.st_dev, before.st_ino) != (opened.st_dev, opened.st_ino):
            raise ValueError('asset path changed before inspection')
        result = inspect_stream(source, ASSET_SIZE, ASSET_SHA256, deadline)
        after = path.lstat()
        if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) != (
                after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns):
            raise ValueError('asset path changed during inspection')
        return result
