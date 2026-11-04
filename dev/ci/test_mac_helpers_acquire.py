import hashlib
import errno
import io
import json
from pathlib import Path
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
                self.assertTrue((parent / ".mac-helper-acquisition.lock").exists())
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
            self.assertEqual(list(parent.iterdir()), [])
            self.assertFalse(lease.exists())


if __name__ == "__main__":
    unittest.main()
