import hashlib
import errno
import io
import ssl
import traceback
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile
import urllib.error

import mac_helpers

SCRATCH = Path(__file__).resolve().parents[2] / "deps"


class MacHelpersTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SCRATCH.mkdir(exist_ok=True)

    def test_storage_classifier_checks_all_exception_edges(self):
        errors = [errno.ENOSPC]
        if hasattr(errno, "EDQUOT"):
            errors.append(errno.EDQUOT)
        for code in errors:
            self.assertTrue(mac_helpers.storage_exhausted(OSError(code, "full")))
            for edge in ("reason", "__cause__", "__context__"):
                with self.subTest(code=code, edge=edge):
                    outer = urllib.error.URLError("string reason")
                    setattr(outer, edge, OSError(code, "full"))
                    self.assertTrue(mac_helpers.storage_exhausted(outer))
            outer = urllib.error.URLError("non-storage reason")
            outer.__cause__ = OSError(code, "full")
            self.assertTrue(mac_helpers.storage_exhausted(outer))
        for value in (None, "non-exception", OSError(-1, "unrelated"),
                      urllib.error.URLError(ssl.SSLCertVerificationError("certificate"))):
            self.assertFalse(mac_helpers.storage_exhausted(value))
        with patch.object(mac_helpers.errno, "EDQUOT", create=True):
            delattr(mac_helpers.errno, "EDQUOT")
            self.assertFalse(mac_helpers.storage_exhausted(OSError(-1, "unrelated")))

    def test_storage_classifier_handles_cycles_and_shared_branches(self):
        first, second = RuntimeError("first"), RuntimeError("second")
        first.__cause__ = first
        self.assertFalse(mac_helpers.storage_exhausted(first))
        first.__cause__ = second
        first.__context__ = second
        second.__context__ = first
        self.assertFalse(mac_helpers.storage_exhausted(first))
        second.__cause__ = OSError(errno.ENOSPC, "full")
        self.assertTrue(mac_helpers.storage_exhausted(first))

    def test_close_failure_preserves_original_and_records_secondary(self):
        class BadClose:
            def close(self):
                raise OSError("close failed")
        primary = ValueError("original")
        try:
            with mac_helpers.preserving_close(BadClose()):
                raise primary
        except ValueError as caught:
            self.assertIs(caught, primary)
            self.assertTrue(any(frame.name == "test_close_failure_preserves_original_and_records_secondary"
                                for frame in traceback.extract_tb(caught.__traceback__)))
        else:
            self.fail("primary error was not raised")
        self.assertTrue(primary.helper_cleanup_failed)
        notes = getattr(primary, "__notes__", getattr(primary, "helper_cleanup_notes", []))
        self.assertIn("close cleanup failed: OSError", notes)
        with self.assertRaises(OSError) as close_only:
            with mac_helpers.preserving_close(BadClose()):
                pass
        self.assertTrue(close_only.exception.helper_cleanup_failed)

    def test_copy_unlink_failure_preserves_primary(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            path = Path(directory) / "asset"
            with patch.object(mac_helpers.os, "unlink", side_effect=PermissionError("denied")):
                with self.assertRaises(ValueError) as caught:
                    mac_helpers.checked_copy(io.BytesIO(b"x"), path, 1, "0" * 64)
            self.assertTrue(caught.exception.helper_cleanup_failed)
            self.assertTrue(path.exists())

    def test_zip_rmdir_failure_preserves_primary(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            root = Path(directory)
            archive = root / "bad.zip"
            archive.write_bytes(b"invalid")
            with patch.object(Path, "rmdir", side_effect=PermissionError("denied")):
                with self.assertRaises(ValueError) as caught:
                    mac_helpers.extract_deno(archive, root / "stage", "0" * 64, 10)
            self.assertTrue(caught.exception.helper_cleanup_failed)
            self.assertTrue((root / "stage").exists())

    def test_copy_cleans_private_stage_on_mismatch(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            path = Path(directory) / "asset"
            for data, size, digest in [(b"a", 2, "0" * 64),
                                       (b"abc", 2, "0" * 64),
                                       (b"ab", 2, "0" * 64)]:
                with self.assertRaises(ValueError):
                    mac_helpers.checked_copy(io.BytesIO(data), path, size, digest)
                self.assertFalse(path.exists())
            path.write_bytes(b"existing")
            with self.assertRaises(FileExistsError):
                mac_helpers.checked_copy(io.BytesIO(b""), path, 0, "0" * 64)
            self.assertEqual(path.read_bytes(), b"existing")

    def test_zip_validates_member_expansion_and_digest(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            root = Path(directory)
            archive = root / "deno.zip"
            digest = hashlib.sha256(b"content").hexdigest()
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("deno", b"content")
            for limit, expected in [(6, digest), (7, "0" * 64)]:
                with self.assertRaises(ValueError):
                    mac_helpers.extract_deno(archive, root / "stage", expected, limit)
                self.assertFalse((root / "stage").exists())
            result = mac_helpers.extract_deno(archive, root / "stage", digest, 7)
            self.assertEqual(result.read_bytes(), b"content")

    def test_zip_rejects_unexpected_duplicate_and_symlink(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            root = Path(directory)
            archive = root / "deno.zip"
            for names in [("../deno",), ("deno", "other"), ("deno", "deno")]:
                with zipfile.ZipFile(archive, "w") as zf:
                    for name in names:
                        zf.writestr(name, b"x")
                with self.assertRaises(ValueError):
                    mac_helpers.extract_deno(archive, root / "stage", "0" * 64, 10)
            with zipfile.ZipFile(archive, "w") as zf:
                entry = zipfile.ZipInfo("deno")
                entry.create_system = 3
                entry.external_attr = 0o120777 << 16
                zf.writestr(entry, b"target")
            with self.assertRaises(ValueError):
                mac_helpers.extract_deno(archive, root / "stage", "0" * 64, 10)
            self.assertFalse((root / "stage").exists())

    def test_paths_are_arguments_not_shell_fragments(self):
        root = Path.cwd() / "a space"
        args = mac_helpers.cli_arguments(root / "yt-dlp", root / "deno")
        self.assertEqual(args[0], str(root / "yt-dlp"))
        self.assertEqual(args[-1], f"deno:{root / 'deno'}")
        self.assertEqual(args[1:-1], ["--ignore-config", "--no-plugin-dirs",
                         "--no-update", "--no-remote-components",
                         "--no-js-runtimes", "--js-runtimes"])
        with self.assertRaises(ValueError):
            mac_helpers.cli_arguments("relative", root / "deno")

    def test_malformed_zip_cleans_private_stage(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            root = Path(directory)
            archive = root / "bad.zip"
            archive.write_bytes(b"not a zip")
            with self.assertRaises(ValueError):
                mac_helpers.extract_deno(archive, root / "stage", "0" * 64, 10)
            self.assertFalse((root / "stage").exists())

    def test_zip_directory_admission_precedes_parser(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            root = Path(directory)
            archive = root / "deno.zip"
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("deno", b"x")
            raw = bytearray(archive.read_bytes())
            end = raw.rindex(b"PK\x05\x06")
            raw[end + 10:end + 12] = b"\xff\xff"
            archive.write_bytes(raw)
            with patch.object(mac_helpers.zipfile, "ZipFile") as parser:
                with self.assertRaises(ValueError):
                    mac_helpers.extract_deno(archive, root / "stage", "0" * 64, 10)
            parser.assert_not_called()
            self.assertFalse((root / "stage").exists())

            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("deno", b"x")
                zf.writestr("other", b"y")
            raw = bytearray(archive.read_bytes())
            end = raw.rindex(b"PK\x05\x06")
            raw[end + 8:end + 12] = b"\x01\x00\x01\x00"
            archive.write_bytes(raw)
            with patch.object(mac_helpers.zipfile, "ZipFile") as parser:
                with self.assertRaises(ValueError):
                    mac_helpers.extract_deno(archive, root / "stage", "0" * 64, 10)
            parser.assert_not_called()

    def test_encrypted_and_existing_stage_are_rejected(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            root = Path(directory)
            archive = root / "deno.zip"
            with zipfile.ZipFile(archive, "w") as zf:
                zf.writestr("deno", b"x")
            raw = bytearray(archive.read_bytes())
            local = raw.index(b"PK\x03\x04")
            central = raw.index(b"PK\x01\x02")
            raw[local + 6] |= 1
            raw[central + 8] |= 1
            archive.write_bytes(raw)
            with self.assertRaises(ValueError):
                mac_helpers.extract_deno(archive, root / "stage", "0" * 64, 10)
            stage = root / "stage"
            stage.mkdir()
            (stage / "keep").write_bytes(b"untouched")
            with self.assertRaises(FileExistsError):
                mac_helpers.extract_deno(archive, stage, "0" * 64, 10)
            self.assertEqual((stage / "keep").read_bytes(), b"untouched")


if __name__ == "__main__":
    unittest.main()
