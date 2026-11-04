"""Bounded PyInstaller 6.22 CArchive structure validation; no extraction."""
import struct
import time
import unicodedata

MAGIC = b'MEI\014\013\012\013\016'
COOKIE = struct.Struct('!8sIIII64s')
HEADER = struct.Struct('!IIIIBc')
TOC_LIMIT = 8 * 1024 * 1024
ENTRY_LIMIT = 4096
NAME_LIMIT = 4096
FILE_TYPES = {b'b', b'x', b'Z'}
OTHER_TYPES = {b'z', b'M', b'm', b's', b'o'}


def check_deadline(deadline):
    if time.monotonic() > deadline:
        raise ValueError('inspection deadline exceeded')


def read_at(source, offset, size, deadline=None):
    source.seek(offset)
    chunks = []
    while size:
        if deadline is not None:
            check_deadline(deadline)
        chunk = source.read(min(size, 65536))
        if not chunk:
            raise ValueError('truncated input')
        chunks.append(chunk)
        size -= len(chunk)
    return b''.join(chunks)


def last_cookie(source, size, deadline):
    end = size
    while end:
        check_deadline(deadline)
        start = max(0, end - 65536)
        chunk = read_at(source, start, end - start)
        pos = chunk.rfind(MAGIC)
        if pos >= 0:
            return start + pos
        if start == 0:
            break
        end = start + len(MAGIC) - 1
    raise ValueError('cookie not found')


def cookie_info(source, size, deadline):
    pos = last_cookie(source, size, deadline)
    _, archive_len, toc_off, toc_len, pyver, lib = COOKIE.unpack(
        read_at(source, pos, COOKIE.size))
    start = pos + COOKIE.size - archive_len
    if (start < 0 or toc_len > TOC_LIMIT or toc_len == 0 or not pyver or
            toc_off > archive_len or start + toc_off + toc_len != pos):
        raise ValueError('invalid archive cookie or TOC range')
    if b'\0' not in lib:
        raise ValueError('unterminated Python library name')
    name, padding = lib.split(b'\0', 1)
    if not name or any(padding):
        raise ValueError('invalid Python library reference')
    return start, toc_off, toc_len, name, padding, pos


def safe_name(raw):
    try:
        name = raw.decode('utf-8')
    except UnicodeDecodeError as exc:
        raise ValueError('invalid UTF-8 name') from exc
    parts = name.split('/')
    if (not name or len(raw) > NAME_LIMIT or len(parts) > 32 or
            any(part in ('', '.', '..') for part in parts) or '\\' in name or
            ':' in name or any(ord(char) < 32 for char in name)):
        raise ValueError('unsafe archive path')
    return name


def parse_entries(data, toc_off, deadline):
    entries, options, names, ranges = [], [], set(), []
    position = 0
    while position < len(data):
        check_deadline(deadline)
        if len(entries) + len(options) >= ENTRY_LIMIT or len(data) - position < HEADER.size:
            raise ValueError('invalid entry count or header')
        length, offset, stored, expanded, flag, kind = HEADER.unpack_from(data, position)
        if (length < HEADER.size + 1 or length > HEADER.size + NAME_LIMIT + 16 or
                length % 16 or position + length > len(data)):
            raise ValueError('invalid entry boundary')
        field = data[position + HEADER.size:position + length]
        if b'\0' not in field:
            raise ValueError('unterminated entry')
        raw, padding = field.split(b'\0', 1)
        if any(padding) or len(raw) > NAME_LIMIT or kind not in FILE_TYPES | OTHER_TYPES or flag not in (0, 1):
            raise ValueError('invalid entry fields')
        if offset + stored > toc_off:
            raise ValueError('member outside payload')
        if not flag and stored != expanded:
            raise ValueError('uncompressed length mismatch')
        if kind == b'o':
            option = raw.decode('utf-8')
            if not option or stored or expanded or flag or option.startswith('pyi-runtime-tmpdir'):
                raise ValueError('unsupported runtime option')
            options.append(option)
        else:
            name = safe_name(raw)
            key = unicodedata.normalize('NFD', name).casefold()
            if key in names or any(key.startswith(x + '/') or x.startswith(key + '/') for x in names):
                raise ValueError('colliding output path')
            names.add(key)
            entries.append((name, offset, stored, expanded, flag, kind))
            if stored:
                ranges.append((offset, offset + stored))
        position += length
    ranges.sort()
    if any(a[1] > b[0] for a, b in zip(ranges, ranges[1:])):
        raise ValueError('overlapping member ranges')
    return entries, options
