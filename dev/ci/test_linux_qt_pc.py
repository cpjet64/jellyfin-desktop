import hashlib
import json
import os
from pathlib import Path
import stat
import tempfile
import unittest
from unittest.mock import patch

import linux_qt_pc as pc
import linux_qt_archive


def make_fixture(workspace, contents):
    prefix = workspace / "qt-candidate/6.9.3/gcc_64"
    directory = prefix / "lib/pkgconfig"
    directory.mkdir(parents=True)
    pins = []
    for index, data in enumerate(contents):
        name = f"Qt6Fixture{index}.pc"
        path = directory / name
        path.write_bytes(data)
        mode = stat.S_IMODE(path.stat().st_mode)
        pins.append({"name": name, "bytes": len(data), "mode": mode, "sha256": pc.digest(data),
                     "after_sha256": pc.digest(pc.transformed(data))})
    return prefix, directory, pins


class QtPcTest(unittest.TestCase):
    def test_pins_match_reviewed_inventory(self):
        pins = pc.load_pins()
        source = Path(__file__).resolve().parents[2] / "docs/validation/linux-qt-pc-inventory-2026-11-04.json"
        reviewed = json.loads(source.read_text(encoding="utf-8"))["files"]
        self.assertEqual(len(pins), 70)
        self.assertEqual([(x["name"], x["sha256"], x["after_sha256"], x["mode"]) for x in pins],
                         [(x["name"], x["before_sha256"], x["proposed_after_sha256"], 0o644) for x in reviewed])

    def test_lf_crlf_and_spaces_preserve_other_bytes(self):
        for newline in (b"\n", b"\r\n"):
            with self.subTest(newline=newline), tempfile.TemporaryDirectory(prefix="space test ") as temp:
                root = Path(temp).resolve()
                data = pc.OLD + newline + b"Name: Qt\nLibs: -L${prefix}/lib\n"
                prefix, directory, pins = make_fixture(root, [data])
                with patch.object(pc, "load_pins", return_value=pins):
                    pc.normalize_qt_pc(prefix, root)
                    evidence = pc.verify_normalized(prefix, root)
                self.assertEqual((directory / pins[0]["name"]).read_bytes(), data.replace(pc.OLD, pc.NEW, 1))
                self.assertEqual(evidence["count"], 1)

    def test_invalid_last_input_never_writes(self):
        data = pc.OLD + b"\nName: Qt\n"
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            prefix, directory, pins = make_fixture(root, [data, data])
            last = directory / pins[-1]["name"]
            last.write_bytes(b"x" * len(data))
            before = {p.name: p.read_bytes() for p in directory.iterdir()}
            with patch.object(pc, "load_pins", return_value=pins), self.assertRaises(ValueError):
                pc.normalize_qt_pc(prefix, root)
            self.assertEqual(before, {p.name: p.read_bytes() for p in directory.iterdir()})
            self.assertFalse((root / pc.MARKER).exists())

    def test_invalid_syntax_fails(self):
        samples = (b"prefix=/other\n", pc.OLD + b"\n" + pc.OLD + b"\n",
                   pc.OLD + b"\nother=/home/qt/work/install/lib\n", pc.OLD + b"\\\nnext\n",
                   pc.OLD + b"\rmalformed\n", b"prefix =/home/qt/work/install\n")
        for data in samples:
            with self.subTest(data=data), self.assertRaises(ValueError):
                pc.transformed(data)

    def test_extra_missing_mode_and_link_fail_without_writes(self):
        data = pc.OLD + b"\n"
        for action in ("extra", "missing", "mode", "link"):
            with self.subTest(action=action), tempfile.TemporaryDirectory() as temp:
                root = Path(temp).resolve()
                prefix, directory, pins = make_fixture(root, [data])
                item = directory / pins[0]["name"]
                if action == "extra":
                    (directory / "extra.pc").write_bytes(b"x")
                elif action == "missing":
                    item.unlink()
                elif action == "mode":
                    pins[0]["mode"] ^= 0o200
                else:
                    with patch.object(Path, "lstat", return_value=type("Info", (), {"st_mode": stat.S_IFLNK})()):
                        with self.assertRaises(ValueError):
                            pc.checked_read(item, pins[0]["mode"], len(data))
                    continue
                with patch.object(pc, "load_pins", return_value=pins), self.assertRaises(ValueError):
                    pc.normalize_qt_pc(prefix, root)
                self.assertFalse((root / pc.MARKER).exists())

    def test_tamper_and_mixed_state_fail(self):
        data = pc.OLD + b"\n"
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp).resolve()
            prefix, directory, pins = make_fixture(root, [data, data])
            with patch.object(pc, "load_pins", return_value=pins):
                with patch.object(pc.os, "replace", side_effect=OSError("write failed")):
                    with self.assertRaises(OSError):
                        pc.normalize_qt_pc(prefix, root)
                with self.assertRaisesRegex(ValueError, "already attempted"):
                    pc.normalize_qt_pc(prefix, root)
            self.assertFalse((root / "qt-sdk-ready.json").exists())

    def test_changed_output_or_extra_file_rejected(self):
        data = pc.OLD + b"\n"
        for action in ("changed", "extra", "manifest"):
            with self.subTest(action=action), tempfile.TemporaryDirectory() as temp:
                root = Path(temp).resolve()
                prefix, directory, pins = make_fixture(root, [data])
                with patch.object(pc, "load_pins", return_value=pins):
                    pc.normalize_qt_pc(prefix, root)
                    if action == "changed":
                        (directory / pins[0]["name"]).write_bytes(b"x")
                    elif action == "extra":
                        (directory / "extra.pc").write_bytes(b"x")
                    else:
                        (root / pc.MANIFEST).write_text('{}')
                    with self.assertRaises(ValueError):
                        pc.verify_normalized(prefix, root)

    def test_inherited_pkg_config_override_is_rejected(self):
        with patch.dict(os.environ, {}, clear=True), patch.object(pc.shutil, "which", return_value="/usr/bin/pkg-config"):
            pc.validate_pkg_environment()
            for name in ("PKG_CONFIG", "PKG_CONFIG_SYSROOT_DIR", "PKG_CONFIG_DONT_DEFINE_PREFIX"):
                with self.subTest(name=name), patch.dict(os.environ, {name: "/unexpected"}), self.assertRaises(ValueError):
                    pc.validate_pkg_environment()
            with patch.dict(os.environ, {"PKG_CONFIG_PATH": "/unexpected"}), self.assertRaises(ValueError):
                pc.validate_pkg_environment()
            with tempfile.TemporaryDirectory() as temp:
                prefix = Path(temp).resolve()
                allowed = str(prefix / "lib/pkgconfig")
                with patch.dict(os.environ, {"PKG_CONFIG_PATH": allowed}):
                    pc.validate_pkg_environment(prefix)

    def test_ready_rejects_tampered_normalization_evidence_or_outputs(self):
        data = pc.OLD + b"\n"
        for action in ("file", "manifest", "extra"):
            with self.subTest(action=action), tempfile.TemporaryDirectory() as temp:
                root = Path(temp).resolve()
                prefix, directory, pins = make_fixture(root, [data])
                (root / "qt-link-modes.json").write_text("[]\n")
                (root / pc.ZLIB_EVIDENCE).write_text('{"zlib": "same"}\n')
                candidate = linux_qt_archive.Candidate(root, prefix, linux_qt_archive.manifest_hash())
                report = {"qt_prefix": str(prefix), "mpv_include": str(root / "include"),
                          "mpv_library": str(root / "libmpv.so"),
                          "environment": {"PKG_CONFIG_PATH": str(prefix / "lib/pkgconfig"),
                                          "LD_LIBRARY_PATH": str(prefix / "lib")}}
                with patch.object(pc, "load_pins", return_value=pins), \
                        patch.object(pc, "host_zlib", return_value={"zlib": "same"}):
                    pc.normalize_qt_pc(prefix, root)
                    if action == "file":
                        (directory / pins[0]["name"]).write_bytes(b"tampered")
                    elif action == "manifest":
                        (root / pc.MANIFEST).write_text('{}')
                    else:
                        (directory / "extra.pc").write_bytes(b"x")
                    with self.assertRaises(ValueError):
                        linux_qt_archive.complete_qt(candidate, report)
                self.assertFalse((root / "qt-sdk-ready.json").exists())


if __name__ == "__main__":
    unittest.main()
