import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from qt_sdk_archive import Entry, check_storage, download_archive, plan_entries, stage_entries, validate_manifest, verify_archive


ROOT = "6.9.3/gcc_64"


def file_entry(path, data=b"ok", mode=0o755, size=None):
    return Entry(path, "file", len(data) if size is None else size, mode,
                 open_data=lambda: io.BytesIO(data))


class QtArchiveTest(unittest.TestCase):
    def test_manifest(self):
        manifest = json.loads(Path(__file__).with_name("qt-6.9.3-unix.json").read_text())
        linux = manifest["platforms"]["linux"]
        entries = validate_manifest(linux)
        self.assertEqual(len(entries), 10)
        self.assertEqual(entries[6]["extract_subdirectory"], ROOT + "/lib")
        linux["archive_bytes_total"] += 1
        with self.assertRaises(ValueError):
            validate_manifest(linux)

    def test_download_and_verify_identity(self):
        data = b"archive"
        item = dict(url="https://download.qt.io/a.7z", bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "archive"
            with patch("qt_sdk_archive.urllib.request.urlopen", return_value=io.BytesIO(data)):
                download_archive(item, path)
            verify_archive(item, path)
            for invalid in (dict(item, bytes=len(data) + 1), dict(item, sha256="0" * 64)):
                with self.assertRaises(ValueError):
                    verify_archive(invalid, path)
            with patch("qt_sdk_archive.urllib.request.urlopen", return_value=io.BytesIO(data[:-1])):
                with self.assertRaises(ValueError):
                    download_archive(item, Path(directory) / "short")

    def test_rejects_unsafe_members_before_writes(self):
        entries = [Entry("../escape", "file", 0, open_data=lambda: io.BytesIO()),
                   Entry("/absolute", "directory"), Entry("a", "device"),
                   Entry("link", "symlink", mode=0o777, link="../../../outside"),
                   Entry("a", "symlink", mode=0o777, link="target")]
        for entry in entries[:-1]:
            with self.subTest(entry=entry), self.assertRaises(ValueError):
                plan_entries([(ROOT, [entry])], 100, ROOT)
        with self.assertRaises(ValueError):
            plan_entries([(ROOT, [entries[-1], file_entry("a/child")])], 100, ROOT)
        with self.assertRaises(ValueError):
            plan_entries([(ROOT, [Entry("a", "symlink", mode=0o777, link="."),
                                 Entry("b", "symlink", mode=0o777, link="a/../outside")])], 100, ROOT)
        with self.assertRaises(ValueError):
            plan_entries([(ROOT, [Entry("a", "symlink", mode=0o777, link="b"),
                                 Entry("b", "symlink", mode=0o777, link="a")])], 100, ROOT)

    def test_overlay_link_permissions_and_publication(self):
        entries = [Entry("bin", "directory", mode=0o755), file_entry("bin/tool"),
                   Entry("bin/alias", "symlink", mode=0o777, link="tool")]
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            if not hasattr(__import__("os"), "symlink"):
                self.skipTest("symlinks unavailable")
            try:
                result = stage_entries([(ROOT, entries), (ROOT, [file_entry("bin/tool")])],
                                       parent / "stage", parent / "ready", 20, ROOT)
            except OSError as error:
                if getattr(error, "winerror", None) == 1314:
                    self.skipTest("Windows symlink privilege unavailable")
                raise
            self.assertEqual((result / ROOT / "bin/tool").read_bytes(), b"ok")
            self.assertEqual((result / ROOT / "bin/alias").read_bytes(), b"ok")
            self.assertTrue((result / ROOT / "bin/alias").is_symlink())
            if __import__("os").name != "nt":
                self.assertEqual((result / ROOT / "bin/tool").stat().st_mode & 0o777, 0o755)
            self.assertFalse((parent / "stage").exists())

    def test_valid_relative_link_graph_and_regular_publication(self):
        plan_entries([(ROOT, [file_entry("lib/real"), Entry("lib/soname", "symlink", mode=0o777, link="real"),
                              Entry("lib/name", "symlink", mode=0o777, link="soname")])], 20, ROOT)
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            result = stage_entries([(ROOT, [file_entry("bin/tool"), file_entry("bin/tool")])],
                                   parent / "stage", parent / "ready", 20, ROOT)
            self.assertEqual((result / ROOT / "bin/tool").read_bytes(), b"ok")
            self.assertFalse((parent / "stage").exists())

    def test_directory_and_link_modes(self):
        directory = Entry("bin", "directory", mode=0o755)
        for other in (Entry("bin", "directory", mode=0o700),
                      Entry("alias", "symlink", mode=0o700, link="bin")):
            first = directory if other.kind == "directory" else Entry("alias", "symlink", mode=0o777, link="bin")
            with self.subTest(other=other), self.assertRaises(ValueError):
                plan_entries([(ROOT, [first, other])], 20, ROOT)
        with tempfile.TemporaryDirectory() as workspace:
            parent = Path(workspace)
            chmod = Path.chmod
            with patch.object(Path, "chmod", autospec=True, side_effect=chmod) as applied:
                result = stage_entries([(ROOT, [directory, directory, file_entry("bin/tool")])],
                                       parent / "stage", parent / "ready", 20, ROOT)
            self.assertEqual(applied.call_args_list[-1].args[0].name, "bin")
            self.assertEqual(applied.call_args_list[-1].args[1], 0o755)
            if __import__("os").name != "nt":
                self.assertEqual((result / ROOT / "bin").stat().st_mode & 0o777, 0o755)

    def test_conflicting_overlay_never_publishes(self):
        for second in (file_entry("file", b"different"), file_entry("file", mode=0o644)):
            with tempfile.TemporaryDirectory() as directory:
                parent = Path(directory)
                with self.assertRaises(ValueError):
                    stage_entries([(ROOT, [file_entry("file"), second])], parent / "stage",
                                  parent / "ready", 100, ROOT)
                self.assertFalse((parent / "ready").exists())

    def test_listed_and_actual_expansion_fail_closed(self):
        for entry, limit in ((file_entry("a", b"123"), 2), (file_entry("a", b"123", size=2), 10),
                             (file_entry("a", b"1", size=2), 10)):
            with tempfile.TemporaryDirectory() as directory:
                parent = Path(directory)
                with self.assertRaises(ValueError):
                    stage_entries([(ROOT, [entry])], parent / "stage", parent / "ready", limit, ROOT)
                self.assertFalse((parent / "ready").exists())

    def test_storage_accounts_for_archive_stage_and_build(self):
        with patch("qt_sdk_archive.shutil.disk_usage") as usage:
            usage.return_value = type("Usage", (), {"free": 99})()
            with self.assertRaises(ValueError):
                check_storage(".", 20, 50, 30)
            usage.return_value = type("Usage", (), {"free": 100})()
            self.assertEqual(check_storage(".", 20, 50, 30), 100)


if __name__ == "__main__":
    unittest.main()
