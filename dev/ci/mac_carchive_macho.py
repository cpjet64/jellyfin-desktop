"""Recognize a universal Mach-O layout without modifying or executing it."""
import struct
from mac_carchive_format import check_deadline, read_at


def validate_layout(source, size, archive_start, cookie_end, deadline):
    magic, count = struct.unpack('>II', read_at(source, 0, 8))
    if magic != 0xcafebabe or not 1 <= count <= 32:
        raise ValueError('expected bounded universal Mach-O')
    slices = []
    for index in range(count):
        check_deadline(deadline)
        cpu, subtype, offset, length, align = struct.unpack('>IIIII', read_at(source, 8 + index * 20, 20))
        if offset < 8 + count * 20 or offset + length > size or length < 32:
            raise ValueError('invalid Mach-O slice')
        if align > 30 or offset % (1 << align):
            raise ValueError('invalid slice alignment')
        slice_magic, slice_cpu, slice_subtype = struct.unpack('<III', read_at(source, offset, 12))
        if (slice_magic, slice_cpu, slice_subtype) != (0xfeedfacf, cpu, subtype):
            raise ValueError('slice identity mismatch')
        slices.append((offset, offset + length))
    ordered = sorted(slices)
    if any(a[1] > b[0] for a, b in zip(ordered, ordered[1:])):
        raise ValueError('overlapping Mach-O slices')
    start, end = ordered[-1]
    header = read_at(source, start, 32)
    magic, _, _, _, commands, command_bytes, _, _ = struct.unpack('<8I', header)
    if magic != 0xfeedfacf or commands > 4096 or command_bytes > end - start - 32:
        raise ValueError('invalid final Mach-O header')
    cursor = start + 32
    limit = cursor + command_bytes
    linkedit = None
    signature = None
    for _ in range(commands):
        check_deadline(deadline)
        command, length = struct.unpack('<II', read_at(source, cursor, 8))
        if length < 8 or cursor + length > limit:
            raise ValueError('invalid Mach-O command')
        if command == 0x19 and length >= 72:
            segment = read_at(source, cursor, 72)
            name = segment[8:24].rstrip(b'\0')
            fileoff, filesize = struct.unpack_from('<QQ', segment, 40)
            if (name == b'__LINKEDIT' and start + fileoff <= archive_start < cookie_end
                    <= start + fileoff + filesize <= end):
                if linkedit is not None:
                    raise ValueError('multiple linkedit segments')
                linkedit = (start + fileoff, start + fileoff + filesize)
        if command == 0x1d:
            if length != 16 or signature is not None:
                raise ValueError('invalid signature command')
            offset, length_bytes = struct.unpack('<II', read_at(source, cursor + 8, 8))
            signature = (start + offset, start + offset + length_bytes)
        cursor += length
    if (cursor != limit or linkedit is None or
            (signature is not None and not cookie_end <= signature[0] < signature[1] <= linkedit[1])):
        raise ValueError('cookie outside final slice linkedit')
