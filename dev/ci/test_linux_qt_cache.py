import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import linux_qt_archive as archive
import linux_qt_cache as cache
import linux_qt_pc as pc


def fixture(root):
    sources = root / "source cache"
    sources.mkdir()
    items, mapping = [], {}
    for index in range(10):
        data = f"archive {index}".encode()
        path = sources / f"{index:02d}.7z"
        path.write_bytes(data)
        item = {"url": f"https://download.qt.io/{index}.7z", "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest()}
        items.append(item)
        mapping[item["url"]] = str(path)
    return items, mapping


class CacheTest(unittest.TestCase):
    def test_valid_cache_is_copied_and_verified_without_download(self):
        with tempfile.TemporaryDirectory(prefix="cache test ") as temporary:
            root = Path(temporary).resolve()
            items, mapping = fixture(root)
            before = {path: Path(path).read_bytes() for path in mapping.values()}
            workspace = root / "fresh job"
            workspace.mkdir()
            platform = {"archive_bytes_total": sum(item["bytes"] for item in items),
                        "target_subdirectory": "6.9.3/gcc_64"}
            with patch.object(archive, "linux_items", return_value=(platform, items)), \
                    patch.object(archive, "check_storage"), patch.object(archive, "load_library"), \
                    patch.object(archive, "manifest_hash", return_value="a" * 64), \
                    patch.object(archive, "download_archive") as downloader, \
                    patch.object(archive, "scan_archives", side_effect=RuntimeError("decoder boundary")) as scan:
                with self.assertRaisesRegex(RuntimeError, "decoder boundary"):
                    archive.prepare(workspace, mapping)
            downloader.assert_not_called()
            self.assertEqual(len(scan.call_args.args[0]), 10)
            evidence = cache.verify_evidence(workspace, items, "a" * 64)
            self.assertEqual(evidence["count"], 10)
            self.assertEqual([r["sha256"] for r in evidence["archives"]], [i["sha256"] for i in items])
            self.assertEqual(before, {path: Path(path).read_bytes() for path in mapping.values()})
            for index, item in enumerate(items):
                copied = workspace / "qt-archives" / f"{index:02d}.7z"
                self.assertEqual(copied.read_bytes(), before[mapping[item["url"]]])
                self.assertNotEqual(copied.stat().st_ino, Path(mapping[item["url"]]).stat().st_ino)

    def test_missing_wrong_and_duplicate_mapping_never_falls_back(self):
        for kind in ("missing", "wrong", "duplicate", "relative"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve()
                items, mapping = fixture(root)
                if kind == "missing":
                    del mapping[items[-1]["url"]]
                elif kind == "wrong":
                    mapping["https://download.qt.io/other.7z"] = mapping.pop(items[-1]["url"])
                elif kind == "duplicate":
                    mapping[items[-1]["url"]] = mapping[items[0]["url"]]
                else:
                    mapping[items[-1]["url"]] = "relative.7z"
                workspace = root / "job"
                workspace.mkdir()
                with self.assertRaises((ValueError, FileNotFoundError)):
                    cache.copy_archives(items, mapping, workspace, "a" * 64)
                self.assertFalse((workspace / "qt-archives").exists())

    def test_corrupt_wrongly_mapped_and_missing_sources_fail_before_decode(self):
        for kind in ("corrupt", "swapped", "missing"):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary).resolve()
                items, mapping = fixture(root)
                first, last = items[0]["url"], items[-1]["url"]
                if kind == "corrupt":
                    Path(mapping[last]).write_bytes(b"bad")
                elif kind == "swapped":
                    mapping[first], mapping[last] = mapping[last], mapping[first]
                else:
                    Path(mapping[last]).unlink()
                before = {p: Path(p).read_bytes() for p in mapping.values() if Path(p).exists()}
                workspace = root / "job"
                workspace.mkdir()
                platform = {"archive_bytes_total": 1000, "target_subdirectory": "6.9.3/gcc_64"}
                with patch.object(archive, "linux_items", return_value=(platform, items)), \
                        patch.object(archive, "check_storage"), patch.object(archive, "load_library"), \
                        patch.object(archive, "download_archive") as downloader, \
                        patch.object(archive, "scan_archives") as scan:
                    with self.assertRaises((ValueError, FileNotFoundError)):
                        archive.prepare(workspace, mapping)
                downloader.assert_not_called()
                scan.assert_not_called()
                self.assertFalse((workspace / cache.EVIDENCE).exists())
                self.assertEqual(before, {p: Path(p).read_bytes() for p in before})

    def test_existing_destination_and_tampered_copy_refused(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            items, mapping = fixture(root)
            workspace = root / "job"
            workspace.mkdir()
            (workspace / "qt-archives").mkdir()
            with self.assertRaises(FileExistsError):
                cache.copy_archives(items, mapping, workspace, "a" * 64)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            items, mapping = fixture(root)
            workspace = root / "job"
            workspace.mkdir()
            cache.copy_archives(items, mapping, workspace, "a" * 64)
            (workspace / "qt-archives/09.7z").write_bytes(b"changed")
            with self.assertRaises(ValueError):
                cache.verify_evidence(workspace, items, "a" * 64)

    def test_source_symlink_or_hardlink_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            items, mapping = fixture(root)
            selected = Path(mapping[items[-1]["url"]])
            actual = Path.lstat
            for mode, links in ((stat.S_IFLNK | 0o777, 1), (stat.S_IFREG | 0o644, 2)):
                def pretend(path):
                    return SimpleNamespace(st_mode=mode, st_nlink=links) if path == selected else actual(path)
                with self.subTest(mode=mode, links=links), patch.object(Path, "lstat", pretend):
                    with self.assertRaises(ValueError):
                        cache.validate_mapping(items, mapping)

    def test_default_route_still_downloads_and_uses_no_cache_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            items, mapping = fixture(root)
            workspace = root / "job"
            workspace.mkdir()
            platform = {"archive_bytes_total": 1000, "target_subdirectory": "6.9.3/gcc_64"}
            def download(item, path):
                path.write_bytes(Path(mapping[item["url"]]).read_bytes())
            with patch.object(archive, "linux_items", return_value=(platform, items)), \
                    patch.object(archive, "check_storage"), patch.object(archive, "load_library"), \
                    patch.object(archive, "download_archive", side_effect=download) as downloader, \
                    patch.object(archive, "scan_archives", side_effect=RuntimeError("decoder boundary")):
                with self.assertRaisesRegex(RuntimeError, "decoder boundary"):
                    archive.prepare(workspace)
            self.assertEqual(downloader.call_count, 10)
            self.assertFalse((workspace / cache.EVIDENCE).exists())

    def check_ready_receipt(self, mode):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            items, mapping = fixture(root)
            workspace = root / "job"
            workspace.mkdir()
            cache.copy_archives(items, mapping, workspace, "a" * 64)
            binding = cache.verify_evidence(workspace, items, "a" * 64)
            prefix = workspace / "qt-candidate/6.9.3/gcc_64"
            prefix.mkdir(parents=True)
            (workspace / "qt-link-modes.json").write_text("[]\n")
            evidence = workspace / "qt-host-zlib.json"
            pc.write_exclusive(evidence, b'{"zlib": "same"}\n')
            if os.name != "nt":
                evidence.chmod(mode)
                info = evidence.stat()
                self.assertEqual(stat.S_IMODE(info.st_mode), mode)
                self.assertEqual(info.st_uid, os.getuid())
            candidate = archive.Candidate(workspace, prefix, "a" * 64, binding["evidence_sha256"])
            report = {"qt_prefix": str(prefix), "mpv_include": str(workspace / "include"),
                      "mpv_library": str(workspace / "mpv.so"),
                      "environment": {"PKG_CONFIG_PATH": str(prefix / "lib/pkgconfig"),
                                      "LD_LIBRARY_PATH": str(prefix / "lib")}}
            normalization = {"version": "test", "count": 70, "sha256": "b" * 64}
            def fake_link(source, target, **kwargs):
                shutil.copyfile(source, target)
            with patch.object(archive, "manifest_hash", return_value="a" * 64), \
                    patch.object(archive, "linux_items", return_value=({}, items)), \
                    patch("linux_qt_pc.verify_normalized", return_value=normalization), \
                    patch("linux_qt_pc.host_zlib", return_value={"zlib": "same"}), \
                    patch.object(archive.os, "link", side_effect=fake_link):
                if mode != 0o600:
                    with self.assertRaisesRegex(ValueError, "invalid Qt normalization evidence: qt-host-zlib.json"):
                        archive.complete_qt(candidate, report)
                    self.assertFalse((workspace / "qt-sdk-ready.json").exists())
                    return
                archive.complete_qt(candidate, report)
            receipt = json.loads((workspace / "qt-sdk-ready.json").read_text())
            self.assertEqual(receipt["archive_input"], binding)
            self.assertEqual(len(receipt["archive_input"]["archives"]), 10)

    def test_ready_receipt_binds_all_copied_hashes(self):
        self.check_ready_receipt(0o600)

    @unittest.skipIf(os.name == "nt", "POSIX permission bits are not enforced on Windows")
    def test_writable_zlib_evidence_blocks_ready_receipt(self):
        for mode in (0o620, 0o602):
            with self.subTest(mode=oct(mode)):
                self.check_ready_receipt(mode)

    def test_cached_evidence_failure_blocks_ready(self):
        with tempfile.TemporaryDirectory() as temporary:
            workspace = Path(temporary).resolve()
            prefix = workspace / "qt-candidate/6.9.3/gcc_64"
            prefix.mkdir(parents=True)
            candidate = archive.Candidate(workspace, prefix, "a" * 64, "b" * 64)
            report = {"qt_prefix": str(prefix), "mpv_include": str(workspace / "include"),
                      "mpv_library": str(workspace / "mpv.so"),
                      "environment": {"PKG_CONFIG_PATH": str(prefix / "lib/pkgconfig"),
                                      "LD_LIBRARY_PATH": str(prefix / "lib")}}
            with patch.object(archive, "manifest_hash", return_value="a" * 64), \
                    patch.object(archive, "linux_items", return_value=({}, [1] * 10)), \
                    patch("linux_qt_pc.verify_normalized", return_value={}), \
                    patch.object(cache, "verify_evidence", side_effect=ValueError("changed copy")):
                with self.assertRaisesRegex(ValueError, "changed copy"):
                    archive.complete_qt(candidate, report)
            self.assertFalse((workspace / "qt-sdk-ready.json").exists())


if __name__ == "__main__":
    unittest.main()
