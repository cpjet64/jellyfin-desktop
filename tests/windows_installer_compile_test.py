"""Compile the real installer script against isolated runtime fixtures."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class InstallerCompileTest(unittest.TestCase):
    def setUp(self):
        names = ("ISCC", "WINDOWS_VCRUNTIME_DIR", "WINDOWS_VCREDIST_EXE")
        if not all(os.environ.get(name) for name in names):
            self.skipTest("Set ISCC, WINDOWS_VCRUNTIME_DIR and WINDOWS_VCREDIST_EXE")
        self.iscc = Path(os.environ["ISCC"])
        build = ROOT / "build"
        build.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="installer-test-", dir=build)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.deploy = self.root / "fixture with spaces"
        self.deploy.mkdir()
        shutil.copytree(os.environ["WINDOWS_VCRUNTIME_DIR"], self.deploy / "redist")
        shutil.copy2(os.environ["WINDOWS_VCREDIST_EXE"], self.deploy / "vc_redist.x64.exe")
        shutil.copy2(self.iscc, self.deploy / "Jellyfin Desktop.exe")
        source = (ROOT / "bundle/win/JellyfinDesktop.iss.in").read_text()
        for key, value in {"VERSION_STRING": "0.0.0-test", "CMAKE_INSTALL_PREFIX": self.deploy,
                           "PROJECT_SOURCE_DIR": ROOT}.items():
            source = source.replace("@" + key + "@", str(value).replace("\\", "/"))
        self.script = self.root / "test.iss"
        self.script.write_text(source)

    def compile(self):
        return subprocess.run([str(self.iscc), "/Qp", str(self.script)],
                              capture_output=True, text=True, check=False)

    def test_versioned_inputs_compile(self):
        result = self.compile()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unversioned_runtime_fails_compilation(self):
        (self.deploy / "redist/vcruntime140.dll").write_bytes(b"invalid")
        result = self.compile()
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
