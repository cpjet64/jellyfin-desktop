"""Validate selected CMake provenance fields without running a build."""
import json
from pathlib import Path
import tempfile
import unittest

import build_manifest

ROOT = Path(__file__).resolve().parents[2]


class ManifestTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=ROOT, prefix="manifest-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.build = self.root / "build"
        self.build.mkdir()
        self.out = self.root / "evidence"
        self.out.mkdir()
        self.exe = self.build / "fixture"
        self.exe.write_bytes(b"binary")
        patched = self.build / "patched-mpvqt/src"
        patched.mkdir(parents=True)
        (patched.parent / "0001-event-ownership.patch").write_text("patch")
        (patched / "mpvcontroller.cpp").write_text("controller")
        (self.out / "evidence.json").write_text(json.dumps({"source_sha": "abc", "source_dir": str(self.root), "mpvqt_sha": "2ebbd38", "mode": "static"}))
        self.cache = {"CMAKE_HOME_DIRECTORY": str(self.root), "USE_STATIC_MPVQT": "ON",
                      "MPVQT_EXTERNAL_EVENTS": "ON", "Qt6_DIR": "/qt", "MPV_INCLUDE_DIR": "/mpv/include",
                      "MPV_LIBRARY_mpv": "/mpv/lib", "SECRET_UNRELATED": "must-not-be-captured"}

    def capture(self):
        text = "\n".join(f"{key}:STRING={value}" for key, value in self.cache.items())
        (self.build / "CMakeCache.txt").write_text(text)
        build_manifest.capture(self.root, self.out, self.exe)
        return json.loads((self.out / "build-manifest.json").read_text())

    def test_valid_selected_fields(self):
        result = self.capture()
        self.assertEqual(result["source_sha"], "abc")
        self.assertNotIn("SECRET_UNRELATED", result["cmake"])
        self.assertEqual(result["cmake"]["MPV_LIBRARY_mpv"], "/mpv/lib")

    def test_wrong_source_mode_and_missing_dependencies(self):
        for key, value, message in (("CMAKE_HOME_DIRECTORY", "/wrong", "source directory"),
                                    ("MPVQT_EXTERNAL_EVENTS", "OFF", "configuration"),
                                    ("Qt6_DIR", "Qt6_DIR-NOTFOUND", "Qt or mpv"),
                                    ("MPV_LIBRARY_mpv", "NOTFOUND", "Qt or mpv")):
            original = self.cache[key]
            self.cache[key] = value
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, message):
                self.capture()
            self.cache[key] = original


if __name__ == "__main__":
    unittest.main()
