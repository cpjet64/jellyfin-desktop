"""Exercise archive boundaries without network or host changes."""

import hashlib
import io
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.dont_write_bytecode = True
from native_bootstrap import download
from safe_extract import extract_tar, extract_zip

ROOT = Path(__file__).resolve().parents[2]


class TestArchiveBoundaries(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="archive-test-", dir=ROOT)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def tar(self, name, kind=tarfile.REGTYPE):
        archive = self.root / "sample.tar"
        with tarfile.open(archive, "w") as output:
            member = tarfile.TarInfo(name)
            member.type = kind
            if kind == tarfile.REGTYPE:
                member.size = 4
                output.addfile(member, io.BytesIO(b"safe"))
            else:
                member.linkname = "../../outside"
                output.addfile(member)
        return archive

    def test_tar_extracts_regular_files(self):
        destination = self.root / "out"
        destination.mkdir()
        extract_tar(self.tar("dir/file"), destination)
        self.assertEqual((destination / "dir/file").read_bytes(), b"safe")

    def test_tar_rejects_escape_and_links(self):
        for name, kind in (("../outside", tarfile.REGTYPE), ("link", tarfile.SYMTYPE)):
            with self.subTest(name=name), self.assertRaises(ValueError):
                destination = self.root / ("out-" + str(len(list(self.root.iterdir()))))
                destination.mkdir()
                extract_tar(self.tar(name, kind), destination)
        self.assertFalse((self.root / "outside").exists())

    def test_zip_rejects_escape(self):
        archive = self.root / "sample.zip"
        with zipfile.ZipFile(archive, "w") as output:
            output.writestr("../outside", b"bad")
        destination = self.root / "out"
        destination.mkdir()
        with self.assertRaises(ValueError):
            extract_zip(archive, destination)

    def test_zip_rejects_symbolic_links(self):
        archive = self.root / "link.zip"
        with zipfile.ZipFile(archive, "w") as output:
            member = zipfile.ZipInfo("link")
            member.create_system = 3
            member.external_attr = (0o120777 << 16)
            output.writestr(member, "../outside")
        destination = self.root / "out"
        destination.mkdir()
        with self.assertRaises(ValueError):
            extract_zip(archive, destination)

    def test_selected_tar_member_does_not_extract_other_entries(self):
        archive = self.root / "node.tar"
        with tarfile.open(archive, "w") as output:
            for name in ("node/bin/node", "../outside"):
                member = tarfile.TarInfo(name)
                member.size = 4
                output.addfile(member, io.BytesIO(b"safe"))
        destination = self.root / "out"
        destination.mkdir()
        extract_tar(archive, destination, "node/bin/node")
        self.assertEqual((destination / "node/bin/node").read_bytes(), b"safe")
        self.assertFalse((self.root / "outside").exists())

    def test_download_rejects_digest_mismatch(self):
        with patch("native_bootstrap.urllib.request.urlopen", return_value=io.BytesIO(b"safe")):
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                download("https://example.invalid/archive", hashlib.sha256(b"other").hexdigest(), self.root / "file")


if __name__ == "__main__":
    unittest.main()
