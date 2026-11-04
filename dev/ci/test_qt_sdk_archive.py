import hashlib
import io
import json
import os
from pathlib import Path
import stat
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from qt_sdk_archive import Entry, check_storage, download_archive, plan_entries, stage_entries, validate_manifest, verify_archive, verify_materialized_link


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

    def test_directory_trailing_separator_is_narrowly_normalized(self):
        entry = Entry("payload/", "directory", mode=0o755)
        plan = plan_entries([(ROOT, [entry, Entry("payload", "directory", mode=0o755)])], 100, ROOT)
        self.assertEqual(plan[0][0], plan[1][0])
        self.assertEqual(plan[0][1].path, "payload/")
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            result = stage_entries([(ROOT, [entry, file_entry("payload/file")])],
                                   parent / "stage", parent / "ready", 100, ROOT)
            self.assertEqual((result / ROOT / "payload/file").read_bytes(), b"ok")
        for name in ("", "/", "//", "payload//", "./", "../", "/absolute/", "a/../", "a/./", "a\\b/"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                plan_entries([(ROOT, [Entry(name, "directory", mode=0o755)])], 100, ROOT)
        for kind in ("file", "symlink"):
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                plan_entries([(ROOT, [Entry("payload/", kind, mode=0o777,
                                            open_data=lambda: io.BytesIO())])], 100, ROOT)
        with self.assertRaises(ValueError):
            plan_entries([(ROOT, [entry, Entry("payload", "directory", mode=0o700)])], 100, ROOT)
        with self.assertRaises(ValueError):
            plan_entries([(ROOT, [entry, file_entry("payload")])], 100, ROOT)

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
                      Entry("alias", "symlink", mode=0o755, link="bin")):
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

    def test_materialized_link_requires_type_mode_and_exact_target(self):
        path = Path("link")
        for mode, target in ((stat.S_IFREG | 0o777, "real"), (stat.S_IFLNK | 0o755, "real"),
                             (stat.S_IFLNK | 0o777, "other")):
            with self.subTest(mode=mode, target=target), \
                    patch("qt_sdk_archive.os.lstat", return_value=SimpleNamespace(st_mode=mode)), \
                    patch("qt_sdk_archive.os.readlink", return_value=target), self.assertRaises(ValueError):
                verify_materialized_link(path, "real")
        with patch("qt_sdk_archive.os.lstat", return_value=SimpleNamespace(st_mode=stat.S_IFLNK | 0o777)), \
                patch("qt_sdk_archive.os.readlink", return_value="real"):
            self.assertEqual(verify_materialized_link(path, "real"), 0o777)

    def test_link_archive_modes_are_recorded_without_chmod_on_link(self):
        entries = [file_entry("real", mode=0o644), Entry("link755", "symlink", mode=0o755, link="real"),
                   Entry("link777", "symlink", mode=0o777, link="real")]
        records = []
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            chmod = Path.chmod
            with patch("qt_sdk_archive.os.symlink") as create, \
                    patch("qt_sdk_archive.verify_materialized_link", return_value=0o777) as verify, \
                    patch.object(Path, "chmod", autospec=True, side_effect=chmod) as modes:
                stage_entries([(ROOT, entries)], parent / "stage", parent / "ready", 100, ROOT,
                              link_records=records)
            self.assertEqual(create.call_count, 2)
            self.assertEqual(verify.call_count, 2)
            self.assertEqual([(r["archive_mode"], r["native_mode"]) for r in records],
                             [(0o755, 0o777), (0o777, 0o777)])
            self.assertEqual([call.args[1] for call in modes.call_args_list], [0o644])
            self.assertEqual((parent / "ready" / ROOT / "real").read_bytes(), b"ok")

    def test_failed_materialized_link_never_publishes(self):
        entries = [file_entry("real"), Entry("link1", "symlink", mode=0o755, link="real"),
                   Entry("link2", "symlink", mode=0o777, link="real")]
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            verify_stage = Mock()
            with patch("qt_sdk_archive.os.symlink"), \
                    patch("qt_sdk_archive.verify_materialized_link", side_effect=[0o777, ValueError("invalid link")]) as verify:
                with self.assertRaisesRegex(ValueError, "invalid link"):
                    stage_entries([(ROOT, entries)], parent / "stage", parent / "qt-candidate", 100, ROOT,
                                  verify_stage)
            self.assertEqual(verify.call_count, 2)
            verify_stage.assert_not_called()
            self.assertFalse((parent / "qt-candidate").exists())
            self.assertFalse((parent / "qt-sdk-ready.json").exists())

    @unittest.skipUnless(os.name == "posix", "requires native POSIX symbolic links")
    def test_real_link_materialization_preserves_target_mode(self):
        entries = [file_entry("real", mode=0o640), Entry("link", "symlink", mode=0o755, link="real")]
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            records = []
            result = stage_entries([(ROOT, entries)], parent / "stage", parent / "ready", 100, ROOT,
                                   link_records=records)
            self.assertEqual(stat.S_IMODE(os.lstat(result / ROOT / "link").st_mode), 0o777)
            self.assertEqual(stat.S_IMODE((result / ROOT / "real").stat().st_mode), 0o640)
            self.assertEqual(records[0]["archive_mode"], 0o755)
            self.assertEqual(records[0]["native_mode"], 0o777)

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
