import io
import builtins
import errno
import gzip
import hashlib
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import macos_sdist_static as subject


def setUpModule():
    (subject.REPO / "deps").mkdir(exist_ok=True)


class SyntheticGroup(Exception):
    def __init__(self, message, exceptions):
        super().__init__(message)
        self.exceptions = exceptions


class StaticInspectionTests(unittest.TestCase):
    def test_reservation_allocates_and_checks(self):
        with tempfile.TemporaryDirectory(dir=subject.REPO / "deps") as directory:
            with patch.object(subject, "RESERVE_BYTES", 1024 * 1024), \
                    patch.object(subject, "allocated_size", return_value=1024 * 1024):
                subject.reserve(Path(directory))
                self.assertEqual((Path(directory) / "reservation.bin").stat().st_size, 1024 * 1024)

    def test_wrapped_storage_failure(self):
        import urllib.error
        full = OSError(errno.ENOSPC, "full")
        wrapped = urllib.error.URLError(full)
        outer = RuntimeError("masked")
        outer.__cause__ = wrapped
        full.__context__ = outer
        self.assertTrue(subject.storage_failure(outer))
        group_type = getattr(builtins, "ExceptionGroup", SyntheticGroup)
        self.assertTrue(subject.storage_failure(group_type("group", [RuntimeError("x"), full])))
        self.assertFalse(subject.storage_failure(urllib.error.URLError("TLS failure")))

    def archive(self, directory, entries):
        path = Path(directory) / "fixture.tar.gz"
        with tarfile.open(path, "w:gz") as archive:
            for name, data, kind in entries:
                info = tarfile.TarInfo(name)
                info.type = kind
                info.size = len(data) if kind == tarfile.REGTYPE else 0
                archive.addfile(info, io.BytesIO(data) if info.size else None)
        return path

    def test_metadata_and_manifest(self):
        self.assertEqual(sum(x["size"] for x in subject.load_manifest()), 5421756)
        with tempfile.TemporaryDirectory(dir=subject.REPO / "deps") as directory:
            path = self.archive(directory, [("package/pyproject.toml", b"[build-system]\n", tarfile.REGTYPE)])
            expanded, found, _ = subject.inspect_archive(path, 0)
            self.assertGreaterEqual(expanded, 15)
            self.assertEqual(found["text"]["package/pyproject.toml"], "[build-system]\n")

    def test_nested_inventory_and_trailing_gzip(self):
        with tempfile.TemporaryDirectory(dir=subject.REPO / "deps") as directory:
            path = self.archive(directory, [("root/subprojects/foo.wrap", b"x", tarfile.REGTYPE)])
            expanded, found, _ = subject.inspect_archive(path, 0)
            self.assertIn("root/subprojects/foo.wrap", found["text"])
            with patch.object(subject, "EXPANSION_LIMIT", expanded - 1), self.assertRaises(ValueError):
                subject.inspect_archive(path, 0)
            with path.open("ab") as out:
                out.write(gzip.compress(b"extra trailing bytes"))
            with self.assertRaises(ValueError):
                subject.inspect_archive(path, 0)

    def test_transfer_identity_and_extra_byte(self):
        class Response(io.BytesIO):
            pass
        class Opener:
            def __init__(self, data): self.data = data
            def open(self, *args, **kwargs): return Response(self.data)
        entry = {"name": "test.tar.gz", "url": "https://example.invalid", "size": 3,
                 "sha256": hashlib.sha256(b"abc").hexdigest()}
        with tempfile.TemporaryDirectory(dir=subject.REPO / "deps") as directory:
            for data, fails in [(b"abc", False), (b"abcd", True), (b"abd", True)]:
                path = Path(directory) / entry["name"]
                if path.exists(): path.unlink()
                with patch.object(subject.urllib.request, "build_opener", return_value=Opener(data)) as build:
                    if fails:
                        with self.assertRaises(ValueError): subject.acquire(entry, Path(directory), 0)
                    else:
                        self.assertEqual(subject.acquire(entry, Path(directory), 0), 3)
                        proxy = build.call_args.args[0]
                        self.assertIsInstance(proxy, subject.urllib.request.ProxyHandler)
                        self.assertEqual(proxy.proxies, {})
            path.unlink()
            with patch.object(subject, "TRANSFER_LIMIT", 3), patch.object(subject.urllib.request, "build_opener", return_value=Opener(b"abc")):
                with self.assertRaises(ValueError): subject.acquire(entry, Path(directory), 0)

    def test_rejects_traversal_and_links(self):
        with tempfile.TemporaryDirectory(dir=subject.REPO / "deps") as directory:
            for name, kind in [("../escape", tarfile.REGTYPE), ("root/link", tarfile.SYMTYPE),
                               ("C:/escape", tarfile.REGTYPE), ("root\\escape", tarfile.REGTYPE),
                               ("root//file", tarfile.REGTYPE)]:
                path = self.archive(directory, [(name, b"", kind)])
                with self.assertRaises(ValueError):
                    subject.inspect_archive(path, 0)
            path = self.archive(directory, [("root/file", b"x", tarfile.REGTYPE),
                                            ("root/file", b"y", tarfile.REGTYPE)])
            with self.assertRaises(ValueError):
                subject.inspect_archive(path, 0)

    def test_pax_long_path_and_corrupt_header(self):
        with tempfile.TemporaryDirectory(dir=subject.REPO / "deps") as directory:
            path = self.archive(directory, [("root/" + "x" * 150, b"", tarfile.REGTYPE)])
            _, found, _ = subject.inspect_archive(path, 0)
            self.assertIn("root/" + "x" * 150, found["inventory"])
            with self.assertRaises(ValueError):
                subject.parse_header(b"x" * 512)
            header = bytearray(tarfile.TarInfo("root/file").tobuf(format=tarfile.USTAR_FORMAT))
            header[124:136] = b"-1".ljust(12, b"\0")
            header[148:156] = b"        "
            header[148:156] = (f"{sum(header):06o}\0 ").encode("ascii")
            with self.assertRaises(ValueError):
                subject.parse_header(bytes(header))

    def test_pax_record_safety(self):
        def record(value):
            length = len(value) + 3
            while len(str(length)) + len(value) + 2 != length:
                length = len(str(length)) + len(value) + 2
            return str(length).encode() + b" " + value + b"\n"
        self.assertEqual(subject.parse_pax(record(b"size=3")), {"size": "3"})
        for data in (record(b"path=../x") + record(b"path=x"), record(b"linkpath=x"),
                     record(b"size=-1"), b"12 path=x\n", record(b"path=\xff")):
            with self.assertRaises(ValueError): subject.parse_pax(data)
        with self.assertRaises(ValueError): subject.safe_member("../x", b"0")

    def test_total_deadline(self):
        class Opener:
            def open(self, *args, **kwargs): return io.BytesIO(b"abc")
        entry = {"name": "test.tar.gz", "url": "https://example.invalid", "size": 3,
                 "sha256": hashlib.sha256(b"abc").hexdigest()}
        with tempfile.TemporaryDirectory(dir=subject.REPO / "deps") as directory:
            with patch.object(subject.urllib.request, "build_opener", return_value=Opener()), \
                    patch.object(subject.time, "monotonic", side_effect=[0, 121]):
                with self.assertRaises(TimeoutError): subject.acquire(entry, Path(directory), 0)

    def test_expansion_and_metadata_caps(self):
        with tempfile.TemporaryDirectory(dir=subject.REPO / "deps") as directory:
            path = self.archive(directory, [("root/pyproject.toml", b"abc", tarfile.REGTYPE)])
            with patch.object(subject, "EXPANSION_LIMIT", 2), self.assertRaises(ValueError):
                subject.inspect_archive(path, 0)
            with patch.object(subject, "METADATA_LIMIT", 2), self.assertRaises(ValueError):
                subject.inspect_archive(path, 0)


if __name__ == "__main__":
    unittest.main()
