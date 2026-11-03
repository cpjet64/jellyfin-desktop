"""Exercise Windows portable staging with isolated package inputs."""

import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"


class WindowsPackagingRuntimeTest(unittest.TestCase):
    def setUp(self):
        BUILD.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=BUILD, prefix="packaging-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        self.runtime = self.root / "runtime files"
        self.runtime.mkdir()
        for name in ("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll"):
            (self.runtime / name).write_bytes(name.encode())
        self.redist = self.root / "redist.exe"
        self.redist.write_bytes(b"fixture")
        self.install = self.root / "installed"
        self.install.mkdir()
        (self.install / "Jellyfin Desktop.exe").write_bytes(b"fixture")
        (self.install / "vc_redist.x64.exe").write_bytes(b"fixture")
        self.configure_scripts()

    def configure_scripts(self):
        substitutions = {
            "WINDOWS_VCRUNTIME_DIR": self.runtime,
            "WINDOWS_VCREDIST_EXE": self.redist,
            "CMAKE_BINARY_DIR": self.root,
            "CMAKE_CURRENT_BINARY_DIR": self.root,
            "CMAKE_INSTALL_PREFIX": self.install,
            "PROJECT_SOURCE_DIR": ROOT,
        }
        for name in ("ValidateWindowsRuntime", "PreparePortableZip"):
            content = (ROOT / "CMakeModules" / (name + ".cmake.in")).read_text()
            for key, value in substitutions.items():
                content = content.replace("@" + key + "@", value.as_posix())
            (self.root / (name + ".cmake")).write_text(content)

    def run_staging(self):
        return subprocess.run(["cmake", "-P", str(self.root / "PreparePortableZip.cmake")],
                              capture_output=True, text=True, check=False)

    def test_stages_runtime_without_installer(self):
        result = self.run_staging()
        self.assertEqual(result.returncode, 0, result.stderr)
        portable = self.root / "portable"
        self.assertTrue((portable / "vcruntime140.dll").is_file())
        self.assertTrue((portable / "Jellyfin Desktop.exe").is_file())
        self.assertFalse((portable / "vc_redist.x64.exe").exists())

    def test_missing_inputs_fail_before_staging(self):
        self.redist.unlink()
        result = self.run_staging()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("WINDOWS_VCREDIST_EXE", result.stderr)
        self.assertFalse((self.root / "portable").exists())
        self.redist.write_bytes(b"fixture")
        (self.runtime / "msvcp140.dll").unlink()
        result = self.run_staging()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("msvcp140.dll", result.stderr)


if __name__ == "__main__":
    unittest.main()
