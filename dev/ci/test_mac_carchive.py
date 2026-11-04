import hashlib
import io
from pathlib import Path
import struct
import tempfile
import time
import unittest
from unittest.mock import patch
import zlib

import mac_carchive as verifier
from mac_carchive_format import COOKIE, HEADER, MAGIC, parse_entries, read_at

SCRATCH = Path(__file__).resolve().parents[2] / 'deps'


def entry(name, offset, payload, expanded, flag=0, kind=b'b'):
    raw = name.encode() + b'\0'
    length = (HEADER.size + len(raw) + 15) & ~15
    return HEADER.pack(length, offset, len(payload), expanded, flag, kind) + raw.ljust(length - HEADER.size, b'\0')


def fixture(data=b'python', compressed=True):
    payload = zlib.compress(data) if compressed else data
    toc = entry('Python', 0, payload, len(data), int(compressed))
    cookie = COOKIE.pack(MAGIC, len(payload) + len(toc) + COOKIE.size,
                         len(payload), len(toc), 314, b'Python'.ljust(64, b'\0'))
    archive = payload + toc + cookie
    start = 512
    end = start + len(archive) + 32
    fat = (struct.pack('>II', 0xcafebabe, 2) + struct.pack('>IIIII', 0x100000c, 0, 64, 64, 0)
           + struct.pack('>IIIII', 0x1000007, 3, 256, end - 256, 0))
    first = struct.pack('<8I', 0xfeedfacf, 0x100000c, 0, 2, 0, 0, 0, 0)
    header = struct.pack('<8I', 0xfeedfacf, 0x1000007, 3, 2, 2, 88, 0, 0)
    segment = struct.pack('<II16sQQQQIIII', 0x19, 72, b'__LINKEDIT'.ljust(16, b'\0'),
                          0, 0, start - 256, end - start, 0, 0, 0, 0)
    signature = struct.pack('<4I', 0x1d, 16, start + len(archive) - 256, 32)
    return (fat.ljust(64, b'\0') + first.ljust(192, b'\0')
            + (header + segment + signature).ljust(start - 256, b'\0') + archive
            + struct.pack('>III', 0xfade0cc0, 32, 0).ljust(32, b'\0'))


class CArchiveTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SCRATCH.mkdir(exist_ok=True)

    def inspect(self, data):
        with tempfile.TemporaryFile(dir=SCRATCH) as source:
            source.write(data)
            source.flush()
            return verifier.inspect_stream(source, len(data), hashlib.sha256(data).hexdigest(), time.monotonic() + 60)

    def test_universal_signature_tail_and_uncompressed(self):
        for compressed in (True, False):
            result = self.inspect(fixture(compressed=compressed))
            self.assertEqual(result['file_bytes'], 6)

    def test_invalid_last_cookie_never_falls_back(self):
        with self.assertRaises(ValueError):
            self.inspect(fixture() + MAGIC + b'\0' * 80)

    def test_bad_identity(self):
        with tempfile.TemporaryFile(dir=SCRATCH) as source:
            source.write(fixture())
            source.flush()
            with self.assertRaises(ValueError):
                verifier.inspect_stream(source, source.tell(), '0' * 64, time.monotonic() + 60)

    def test_inflater_rejects_truncation_trailing_and_limit(self):
        payload = zlib.compress(b'abc')
        for data, limit in ((payload[:-1], 9), (payload + payload, 9), (payload, 2)):
            with self.subTest(data=data, limit=limit):
                with self.assertRaises((ValueError, zlib.error)):
                    verifier.inflate_member(io.BytesIO(data), 0, len(data), limit, time.monotonic() + 60)

    def test_names_ranges_and_options(self):
        cases = [entry('../x', 0, b'a', 1), entry('A', 0, b'a', 1) + entry('a', 1, b'b', 1),
                 entry('a', 0, b'a', 1) + entry('b', 0, b'b', 1),
                 entry('pyi-runtime-tmpdir /tmp', 0, b'', 0, kind=b'o'),
                 entry('special', 0, b'', 0, kind=b'n')]
        for raw in cases:
            with self.subTest(raw=raw):
                with self.assertRaises(ValueError):
                    parse_entries(raw, 10, time.monotonic() + 60)

    def test_toc_policy_and_ordered_options(self):
        deadline = time.monotonic() + 60
        options = entry('first', 2, b'', 0, kind=b'o') + entry('second', 3, b'', 0, kind=b'o')
        self.assertEqual(parse_entries(options, 3, deadline)[1], ['first', 'second'])
        invalid = [entry('é', 0, b'a', 1) + entry('e\u0301', 1, b'b', 1),
                   entry('/'.join(['x'] * 33), 0, b'', 0),
                   entry('x' * 4097, 0, b'', 0),
                   entry('x', 0, b'', 0, kind=b'd'),
                   entry('x', 0, b'', 0, kind=b'l'),
                   entry('x', 0, b'', 1),
                   entry('x', 11, b'x', 1),
                   entry('x', 0, b'', 0)[:-1],
                   HEADER.pack(8192, 0, 0, 0, 0, b'b') + b'x\0'.ljust(8192 - HEADER.size, b'\0'),
                   entry('x', 0, b'', 0, kind=b'o') * 4097]
        for raw in invalid:
            with self.subTest(length=len(raw)):
                with self.assertRaises(ValueError):
                    parse_entries(raw, 10, deadline)
        with self.assertRaises(ValueError):
            parse_entries(entry('x', 0, b'', 0), 0, time.monotonic() - 1)
        self.assertEqual(len(parse_entries(entry('x', 0, b'', 0, kind=b'o') * 4096,
                                           0, time.monotonic() + 60)[1]), 4096)

    def test_cookie_and_macho_rejection(self):
        original = bytearray(fixture())
        cookie = original.rindex(MAGIC)
        variants = []
        bad = original.copy()
        bad[cookie + 16:cookie + 20] = (8 * 1024 * 1024 + 1).to_bytes(4, 'big')
        variants.append(bad)
        bad = original.copy()
        bad[cookie + 24:cookie + 88] = b'Z' * 64
        variants.append(bad)
        bad = original.copy()
        bad[256:260] = b'bad!'
        variants.append(bad)
        bad = original.copy()
        struct.pack_into('<I', bad, 256 + 32 + 72 + 12, len(bad))
        variants.append(bad)
        for data in variants:
            with self.assertRaises(ValueError):
                self.inspect(data)

    def test_declared_length_and_python_reference_rejected(self):
        raw = bytearray(fixture())
        cookie = raw.rindex(MAGIC)
        archive_size, toc_offset = struct.unpack_from('!II', raw, cookie + 8)
        start = cookie + COOKIE.size - archive_size
        struct.pack_into('!I', raw, start + toc_offset + 12, 7)
        with self.assertRaises(ValueError):
            self.inspect(raw)

    def test_record_padding_and_flags(self):
        record = bytearray(entry('x', 0, b'', 0))
        record[-1] = 1
        with self.assertRaises(ValueError):
            parse_entries(record, 0, time.monotonic() + 60)
        record = bytearray(entry('x', 0, b'', 0))
        record[16] = 2
        with self.assertRaises(ValueError):
            parse_entries(record, 0, time.monotonic() + 60)
        raw = bytearray(fixture())
        cookie = raw.rindex(MAGIC)
        raw[cookie + 24:cookie + 88] = b'other'.ljust(64, b'\0')
        with self.assertRaises(ValueError):
            self.inspect(raw)

    def test_dictionary_stream_rejected(self):
        compressor = zlib.compressobj(zdict=b'known dictionary')
        data = compressor.compress(b'known dictionary') + compressor.flush()
        with self.assertRaises(zlib.error):
            verifier.inflate_member(io.BytesIO(data), 0, len(data), 100, time.monotonic() + 60)

    def test_read_chunks_are_bounded(self):
        class TrackingReader(io.BytesIO):
            def read(self, size=-1):
                if size < 0 or size > 65536:
                    raise AssertionError('oversized read')
                return super().read(size)
        self.assertEqual(len(read_at(TrackingReader(b'x' * 150000), 0, 150000)), 150000)
        with self.assertRaises(ValueError):
            read_at(TrackingReader(b'x'), 0, 1, time.monotonic() - 1)

    def test_path_replacement_rejected(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            target = Path(directory) / 'asset'
            replacement = Path(directory) / 'replacement'
            target.write_bytes(fixture())
            replacement.write_bytes(fixture())
            original_lstat = Path.lstat
            calls = 0
            def changed(path, *args, **kwargs):
                nonlocal calls
                calls += 1
                return original_lstat(replacement if calls >= 2 else path, *args, **kwargs)
            with patch.object(Path, 'lstat', changed), patch.object(verifier, 'inspect_stream', return_value={}):
                with self.assertRaises(ValueError):
                    verifier.inspect_asset(target)

    def test_nonregular_asset_rejected(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            with self.assertRaises(ValueError):
                verifier.inspect_asset(directory)


if __name__ == '__main__':
    unittest.main()
