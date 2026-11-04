import hashlib
import errno
import io
import json
import os
from pathlib import Path
import ssl
import tempfile
import unittest
from unittest import mock
import zipfile
import urllib.error

import mac_helpers_acquire as acquisition

SCRATCH = Path(__file__).resolve().parents[2] / "deps"


class Response(io.BytesIO):
    def geturl(self):
        return "https://github.com/asset"


class AcquisitionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        SCRATCH.mkdir(exist_ok=True)

    def fixture(self, root):
        zipped = io.BytesIO()
        with zipfile.ZipFile(zipped, "w") as archive:
            archive.writestr("deno", b"deno executable")
        objects = {"yt": b"yt bytes", "zip": zipped.getvalue(), "sum": b"checksum"}
        digest = lambda data: hashlib.sha256(data).hexdigest()
        data = {
            "yt_dlp": {"url": "yt", "size": len(objects["yt"]), "sha256": digest(objects["yt"])},
            "deno_zip": {"url": "zip", "size": len(objects["zip"]), "sha256": digest(objects["zip"])},
            "deno_executable": {"checksum_url": "sum", "checksum_bytes": len(objects["sum"]),
                                "checksum_sha256": digest(objects["sum"]), "sha256": digest(b"deno executable")},
            "limits": {"metadata": 100, "acquisition": 1000, "deno_expanded": 100,
                       "workspace": 2000, "free_space": 0},
        }
        manifest = root / "manifest.json"
        manifest.write_text(json.dumps(data), encoding="utf-8")
        return data, objects, manifest

    def test_complete_then_refuse_existing(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            root = Path(directory)
            data, objects, manifest = self.fixture(root)
            parent = root / "private"
            parent.mkdir()
            with mock.patch.object(acquisition, "MANIFEST", manifest):
                result = acquisition._acquire(parent, 0, lambda url, timeout: Response(objects[url]))
                self.assertEqual((result / "extracted" / "deno").read_bytes(), b"deno executable")
                self.assertTrue((result / "READY").is_file())
                self.assertEqual(acquisition.verified_paths(result)[0], result / "yt_dlp")
                (result / "yt_dlp").write_bytes(b"tampered")
                with self.assertRaises(ValueError):
                    acquisition.verified_paths(result)
                with self.assertRaises(ValueError):
                    acquisition._acquire(parent, 0, lambda url, timeout: Response(objects[url]))

    def test_failure_leaves_no_result_or_stage(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            root = Path(directory)
            data, objects, manifest = self.fixture(root)
            parent = root / "private"
            parent.mkdir()
            objects["zip"] += b"excess"
            with mock.patch.object(acquisition, "MANIFEST", manifest):
                with self.assertRaises(ValueError):
                    acquisition._acquire(parent, 0, lambda url, timeout: Response(objects[url]))
            self.assertEqual(list(parent.iterdir()), [])

    def test_budget_rejection_before_network(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            root = Path(directory)
            data, objects, manifest = self.fixture(root)
            parent = root / "private"
            parent.mkdir()
            data["limits"]["acquisition"] = 1
            manifest.write_text(json.dumps(data), encoding="utf-8")
            opener = mock.Mock()
            with mock.patch.object(acquisition, "MANIFEST", manifest):
                with self.assertRaises(ValueError):
                    acquisition._acquire(parent, 0, opener)
            opener.assert_not_called()

    def test_redirect_allowlist_and_commitment_admission(self):
        for url in ("http://github.com/x", "https://evil.example/x",
                    "https://github.com.evil.example/x", "https://user@github.com/x"):
            with self.assertRaises(ValueError):
                acquisition.validate_url(url)
        acquisition.validate_url("https://release-assets.githubusercontent.com/x")
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            root = Path(directory)
            data, objects, manifest = self.fixture(root)
            parent = root / "private"
            parent.mkdir()
            opener = mock.Mock()
            with mock.patch.object(acquisition, "MANIFEST", manifest):
                with self.assertRaises(ValueError):
                    acquisition._acquire(parent, 1 << 100, opener)
            opener.assert_not_called()
            self.assertEqual(list(parent.iterdir()), [])

    def test_storage_failure_preserves_evidence_without_ready(self):
        for target in ("checked_copy", "os.link"):
            with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
                root = Path(directory)
                data, objects, manifest = self.fixture(root)
                parent = root / "private"
                parent.mkdir()
                error = urllib.error.URLError(OSError(errno.ENOSPC, "full"))
                with mock.patch.object(acquisition, "MANIFEST", manifest):
                    with mock.patch("mac_helpers_acquire." + target, side_effect=error):
                        with self.assertRaises(urllib.error.URLError):
                            acquisition._acquire(parent, 0, lambda url, timeout: Response(objects[url]))
                self.assertEqual((parent / ".mac-helper-acquisition.lock").exists(), target == "checked_copy")
                self.assertTrue((parent / "mac-helpers").is_dir())
                self.assertFalse((parent / "mac-helpers" / "READY").exists())

    def test_shared_lease_and_marker_collision_refuse_publication(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            root = Path(directory)
            data, objects, manifest = self.fixture(root)
            parent = root / "private"
            parent.mkdir()
            lease = root / "shared.lock"
            lease.write_bytes(b"other owner")
            opener = mock.Mock()
            with mock.patch.object(acquisition, "MANIFEST", manifest):
                with self.assertRaises(FileExistsError):
                    acquisition._acquire(parent, 0, opener, lease)
                opener.assert_not_called()
                lease.unlink()
                with mock.patch("mac_helpers_acquire.os.link", side_effect=FileExistsError):
                    with self.assertRaises(FileExistsError):
                        acquisition._acquire(parent, 0, lambda url, timeout: Response(objects[url]), lease)
            self.assertTrue((parent / "mac-helpers" / ".READY.pending").exists())
            self.assertFalse((parent / "mac-helpers" / "READY").exists())
            self.assertFalse(lease.exists())

    def test_primary_tls_error_survives_stage_cleanup_failure_and_closes_fd(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            root = Path(directory)
            _, _, manifest = self.fixture(root)
            parent = root / "private"
            parent.mkdir()
            primary = urllib.error.URLError(ssl.SSLCertVerificationError("certificate"))
            close = os.close
            with mock.patch.object(acquisition, "MANIFEST", manifest), \
                    mock.patch.object(acquisition.shutil, "rmtree", side_effect=PermissionError("denied")), \
                    mock.patch.object(acquisition.os, "close", wraps=close) as close_mock:
                with self.assertRaises(urllib.error.URLError) as caught:
                    acquisition._acquire(parent, 0, mock.Mock(side_effect=primary))
            self.assertIs(caught.exception, primary)
            self.assertTrue(primary.helper_cleanup_failed)
            self.assertEqual(close_mock.call_count, 1)
            self.assertTrue((parent / ".mac-helper-acquisition.lock").exists())
            self.assertTrue((parent / "mac-helpers").exists())

    def test_close_failure_without_primary_fails_and_retains_lock(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            root = Path(directory)
            _, objects, manifest = self.fixture(root)
            parent = root / "private"
            parent.mkdir()
            real_close = os.close
            def failed_close(fd):
                real_close(fd)
                raise OSError("close failed")
            with mock.patch.object(acquisition, "MANIFEST", manifest), \
                    mock.patch.object(acquisition.os, "close", side_effect=failed_close) as close_mock:
                with self.assertRaisesRegex(OSError, "close failed"):
                    acquisition._acquire(parent, 0, lambda url, timeout: Response(objects[url]))
            self.assertEqual(close_mock.call_count, 1)
            self.assertTrue((parent / ".mac-helper-acquisition.lock").exists())
            stage = parent / "mac-helpers"
            self.assertEqual((stage / "yt_dlp").read_bytes(), objects["yt"])
            self.assertEqual((stage / "extracted" / "deno").read_bytes(), b"deno executable")
            self.assertTrue((stage / ".READY.pending").exists())
            self.assertFalse((stage / "READY").exists())
            with self.assertRaisesRegex(ValueError, "not ready"):
                acquisition.verified_paths(stage)

    def test_lease_removal_failure_retains_verified_but_unconsumable_stage(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            root = Path(directory)
            _, objects, manifest = self.fixture(root)
            parent = root / "private"
            parent.mkdir()
            lock = parent / ".mac-helper-acquisition.lock"
            real_unlink = Path.unlink
            def failed_unlink(path, *args, **kwargs):
                if path == lock:
                    raise PermissionError("lease removal failed")
                return real_unlink(path, *args, **kwargs)
            with mock.patch.object(acquisition, "MANIFEST", manifest), \
                    mock.patch.object(Path, "unlink", failed_unlink):
                with self.assertRaisesRegex(PermissionError, "lease removal failed"):
                    acquisition._acquire(parent, 0, lambda url, timeout: Response(objects[url]))
            stage = parent / "mac-helpers"
            self.assertTrue(lock.exists())
            self.assertEqual((stage / "yt_dlp").read_bytes(), objects["yt"])
            self.assertEqual((stage / "extracted" / "deno").read_bytes(), b"deno executable")
            self.assertTrue((stage / ".READY.pending").exists())
            self.assertFalse((stage / "READY").exists())
            with self.assertRaisesRegex(ValueError, "not ready"):
                acquisition.verified_paths(stage)

    def test_primary_and_close_failures_keep_primary_and_lease(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            root = Path(directory)
            _, _, manifest = self.fixture(root)
            parent = root / "private"
            parent.mkdir()
            primary = ValueError("primary")
            real_close = os.close
            def failed_close(fd):
                real_close(fd)
                raise OSError("close failed")
            with mock.patch.object(acquisition, "MANIFEST", manifest), \
                    mock.patch.object(acquisition.os, "close", side_effect=failed_close):
                with self.assertRaises(ValueError) as caught:
                    acquisition._acquire(parent, 0, mock.Mock(side_effect=primary))
            self.assertIs(caught.exception, primary)
            self.assertTrue(primary.helper_cleanup_failed)
            self.assertTrue((parent / ".mac-helper-acquisition.lock").exists())

    def test_redirect_failure_cleans_stage_without_publication(self):
        class BadRedirect(Response):
            def geturl(self):
                return "https://unapproved.example/file"
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            root = Path(directory)
            _, _, manifest = self.fixture(root)
            parent = root / "private"
            parent.mkdir()
            with mock.patch.object(acquisition, "MANIFEST", manifest):
                with self.assertRaises(ValueError):
                    acquisition._acquire(parent, 0, lambda *_args, **_kwargs: BadRedirect(b"x"))
            self.assertEqual(list(parent.iterdir()), [])

    def test_stage_cleanup_only_failure_is_reported_and_descriptor_closed(self):
        with tempfile.TemporaryDirectory(dir=SCRATCH) as directory:
            root = Path(directory)
            stage = root / "stage"
            stage.mkdir()
            lock = root / "lock"
            descriptor = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
            close = os.close
            with mock.patch.object(acquisition.shutil, "rmtree", side_effect=PermissionError("denied")), \
                    mock.patch.object(acquisition.os, "close", wraps=close) as close_mock:
                with self.assertRaises(PermissionError):
                    acquisition.finish_acquisition(stage, lock, descriptor, True, None)
            close_mock.assert_called_once_with(descriptor)
            self.assertTrue(lock.exists())


if __name__ == "__main__":
    unittest.main()
