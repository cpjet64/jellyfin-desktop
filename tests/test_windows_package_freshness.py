"""Exercise the real Windows package targets using small native fixtures."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PROJECT = r'''cmake_minimum_required(VERSION 3.16)
project(PackageFixture C)
set(VERSION_STRING "fixture")
add_executable(JellyfinDesktop app.c)
set_target_properties(JellyfinDesktop PROPERTIES OUTPUT_NAME "Jellyfin Desktop" SUFFIX ".exe")
add_executable(fake_iscc fake_iscc.c)
set_target_properties(fake_iscc PROPERTIES SUFFIX ".exe" RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}>")
install(TARGETS JellyfinDesktop RUNTIME DESTINATION .)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/payload/" DESTINATION .)
install(CODE "if(EXISTS \"${CMAKE_SOURCE_DIR}/fail-stage\")\n message(FATAL_ERROR \"fixture staging failure\")\nendif()\nfile(APPEND \"${CMAKE_BINARY_DIR}/stage-count.txt\" \"stage:\${CMAKE_INSTALL_CONFIG_NAME}\\n\")")
include("${CMAKE_SOURCE_DIR}/CMakeModules/WindowsInstaller.cmake")
add_dependencies(innosetup_install fake_iscc)
'''
FAKE = r'''#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
  const char *dir = 0, *name = 0, *stage = getenv("PACKAGE_TEST_STAGE");
  char path[4096], buf[4096]; unsigned char header[128] = {0}; size_t count; FILE *in, *out;
  if (getenv("PACKAGE_TEST_COMPILER_FAIL")) return 1;
  for (int i = 1; i < argc; ++i) {
    if (!strncmp(argv[i], "/O", 2)) dir = argv[i]+2;
    if (!strncmp(argv[i], "/F", 2)) name = argv[i]+2;
  }
  if (!dir || !name || !stage) return 2;
  snprintf(path, sizeof(path), "%s/%s.exe", dir, name);
  out = fopen(path, "wb"); if (!out) return 3;
  if (getenv("PACKAGE_TEST_COMPILER_CORRUPT")) { fwrite("bad", 1, 3, out); return fclose(out); }
  header[0] = 'M'; header[1] = 'Z'; header[60] = 64; header[64] = 'P'; header[65] = 'E';
  fwrite(header, 1, sizeof(header), out);
  snprintf(path, sizeof(path), "%s/data.bin", stage);
  in = fopen(path, "rb"); if (!in) return 4;
  while ((count = fread(buf, 1, sizeof(buf), in))) fwrite(buf, 1, count, out);
  fclose(in); return fclose(out);
}
'''


class PackageFreshnessTest(unittest.TestCase):
    def setUp(self):
        (ROOT / "build").mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="package freshness ", dir=ROOT / "build")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "fixture source"
        self.build = self.root / "fixture build"
        self.source.mkdir()
        (self.source / "CMakeModules").mkdir()
        (self.source / "bundle/win").mkdir(parents=True)
        for name in ("WindowsInstaller.cmake", "WindowsPackageStage.cmake.in",
                     "WindowsPackagePublish.cmake.in"):
            shutil.copy2(ROOT / "CMakeModules" / name, self.source / "CMakeModules" / name)
        (self.source / "CMakeLists.txt").write_text(PROJECT)
        (self.source / "app.c").write_text("int main(void) { return 0; }\n")
        (self.source / "fake_iscc.c").write_text(FAKE)
        (self.source / "bundle/win/JellyfinDesktop.iss.in").write_text('stage=@CMAKE_INSTALL_PREFIX@\n')
        payload = self.source / "payload"
        (payload / "redist").mkdir(parents=True)
        (payload / "redist/vcruntime140.dll").write_bytes(b"runtime")
        (payload / "vc_redist.x64.exe").write_bytes(b"redist")
        (payload / "data.bin").write_bytes(b"old!")
        self.env = os.environ.copy()
        self.env["PACKAGE_TEST_STAGE"] = str(self.build / "windows-package-work/stage")
        self.installer = self.build / "JellyfinDesktop-fixture-x64.exe"
        self.zip = self.build / "JellyfinDesktop-fixture-x64.zip"
        self.configure()

    def command(self, args, expected=True):
        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        result = subprocess.run(["cmake", *args], env=self.env, capture_output=True,
                                text=True, timeout=120, creationflags=flags)
        self.assertEqual(result.returncode == 0, expected, result.stdout + result.stderr)
        return result

    def configure(self):
        self.command(["-S", str(self.source), "-B", str(self.build), "-G", "Ninja",
                      "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
                      "-DISCC=" + str(self.build / "fake_iscc.exe"),
                      "-DCMAKE_INSTALL_PREFIX=" + str(self.root / "untouched")])

    def package(self, target="windows_all", expected=True):
        return self.command(["--build", str(self.build), "--target", target, "--parallel", "2"], expected)

    def contents(self):
        with zipfile.ZipFile(self.zip) as archive:
            return {name.removeprefix("./"): archive.read(name) for name in archive.namelist()
                    if not name.endswith("/")}

    def test_fresh_stage_and_same_timestamp_changes(self):
        self.command(["--build", str(self.build), "--parallel", "2"])
        self.assertFalse(self.installer.exists())
        self.assertFalse(self.zip.exists())
        self.assertFalse((self.build / "stage-count.txt").exists())
        self.package()
        first = self.contents()
        self.assertEqual(first["data.bin"], b"old!")
        self.assertTrue(self.installer.read_bytes().endswith(b"old!"))
        self.assertEqual((self.build / "stage-count.txt").read_text().count("stage"), 1)
        self.assertIn("stage:RelWithDebInfo", (self.build / "stage-count.txt").read_text())
        self.assertNotIn("redist/vcruntime140.dll", first)
        self.assertEqual(first["vcruntime140.dll"], b"runtime")
        self.assertNotIn("vc_redist.x64.exe", first)
        data = self.source / "payload/data.bin"
        old = data.stat()
        data.write_bytes(b"new!")
        os.utime(data, ns=(old.st_atime_ns, old.st_mtime_ns))
        (self.source / "payload/added.txt").write_bytes(b"added")
        (self.source / "app.c").write_text("int main(void) { return 7; }\n")
        self.package()
        second = self.contents()
        self.assertEqual(second["data.bin"], b"new!")
        self.assertEqual(second["added.txt"], b"added")
        self.assertNotEqual(first["Jellyfin Desktop.exe"], second["Jellyfin Desktop.exe"])
        self.assertTrue(self.installer.read_bytes().endswith(b"new!"))
        (self.source / "payload/added.txt").unlink()
        self.package()
        self.assertNotIn("added.txt", self.contents())
        self.assertEqual((self.build / "stage-count.txt").read_text().count("stage"), 3)
        self.assertFalse((self.root / "untouched").exists())

    def test_failures_invalidate_outputs(self):
        self.package()
        (self.source / "fail-stage").touch()
        self.package(expected=False)
        self.assertFalse(self.installer.exists())
        self.assertFalse(self.zip.exists())
        (self.source / "fail-stage").unlink()
        self.package()
        self.env["PACKAGE_TEST_COMPILER_FAIL"] = "1"
        self.package("windows_package", expected=False)
        self.assertFalse(self.installer.exists())
        self.assertFalse(self.zip.exists())
        self.env.pop("PACKAGE_TEST_COMPILER_FAIL")
        self.env["PACKAGE_TEST_COMPILER_CORRUPT"] = "1"
        self.package("windows_package", expected=False)
        self.assertFalse(self.installer.exists())
        self.env.pop("PACKAGE_TEST_COMPILER_CORRUPT")
        self.package()
        publish = self.build / "WindowsPackagePublish.cmake"
        publish.write_text(publish.read_text().replace("-E tar cf", "-E invalid-command"))
        self.package("windows_zip", expected=False)
        self.assertFalse(self.installer.exists())
        self.assertFalse(self.zip.exists())

    def test_individual_public_targets_publish_fresh_artifacts(self):
        self.package("windows_package")
        self.assertTrue(self.installer.is_file())
        self.assertFalse(self.zip.exists())
        self.package("windows_zip")
        self.assertFalse(self.installer.exists())
        self.assertEqual(self.contents()["data.bin"], b"old!")


if __name__ == "__main__":
    unittest.main()
