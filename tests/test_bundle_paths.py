"""Configure and execute the Windows bundle script with real path inputs."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODULES = ROOT / "CMakeModules"

STUB = r'''#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
  FILE *out = fopen(getenv("BUNDLE_ARGS"), "w");
  if (!out) return 1;
  for (int i = 1; i < argc; ++i) fprintf(out, "%s\n", argv[i]);
  return fclose(out);
}
'''

PROJECT = r'''cmake_minimum_required(VERSION 3.16)
project(BundlePathFixture C)
set(APPLE FALSE)
set(WIN32 TRUE)
set(SOURCE_ROOT "${CMAKE_SOURCE_DIR}")
set(QTROOT "${CMAKE_SOURCE_DIR}/mock Qt")
set(WINDOWS_VCRUNTIME_DIR "${CMAKE_SOURCE_DIR}/runtime files")
set(WINDOWS_VCREDIST_EXE "${CMAKE_SOURCE_DIR}/vc_redist.x64.exe")
set(CEC_LIBRARY "C:\\fixture space\\cec.lib")
set(INSTALL_BIN_DIR ".")
if(NOT FIXTURE_MPVQT_KIND)
  set(FIXTURE_MPVQT_KIND SHARED)
endif()
add_library(MpvQt::MpvQt ${FIXTURE_MPVQT_KIND} IMPORTED)
set_target_properties(MpvQt::MpvQt PROPERTIES
  IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/mpvqt files/MpvQt.dll")
add_executable(windeployqt stub.c)
set_target_properties(windeployqt PROPERTIES SUFFIX ".exe"
  RUNTIME_OUTPUT_DIRECTORY "$<1:${QTROOT}/bin>")
include("${CMAKE_SOURCE_DIR}/CMakeModules/CompleteBundle.cmake")
'''


class BundlePathsTest(unittest.TestCase):
    def setUp(self):
        build = ROOT / "build"
        build.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="bundle paths ", dir=build)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "fixture source"
        self.source.mkdir()
        self.binary = self.root / "fixture build"
        self.install = self.root / "installed app"
        self.args_log = self.root / "deployment arguments.txt"
        (self.source / "CMakeLists.txt").write_text(PROJECT)
        (self.source / "stub.c").write_text(STUB)
        self.copy_modules()
        self.make_inputs()

    def copy_modules(self):
        modules = self.source / "CMakeModules"
        modules.mkdir()
        for name in ("CompleteBundle.cmake", "CompleteBundleWin.cmake.in",
                     "ValidateWindowsRuntime.cmake.in"):
            shutil.copy2(MODULES / name, modules / name)

    def make_inputs(self):
        for directory in ("mock Qt/bin", "runtime files", "mpv files", "mpv-fallback", "mpvqt files"):
            (self.source / directory).mkdir(parents=True, exist_ok=True)
        for name in ("vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll"):
            (self.source / "runtime files" / name).write_bytes(name.encode())
        (self.source / "vc_redist.x64.exe").write_bytes(b"redist")
        (self.source / "mock Qt/bin/Qt6Xml.dll").write_bytes(b"qt")
        (self.source / "mpv files/libmpv-2.dll").write_bytes(b"primary")
        (self.source / "mpv-fallback/libmpv-2.dll").write_bytes(b"fallback")
        (self.source / "mpvqt files/MpvQt.dll").write_bytes(b"external-mpvqt")

    def check_bundle(self, mpv_path, shared=True):
        env = os.environ.copy()
        env["BUNDLE_ARGS"] = str(self.args_log)
        self.run_cmake(["-S", str(self.source), "-B", str(self.binary),
                        "-DMPV_LIBRARY=" + mpv_path,
                        "-DFIXTURE_MPVQT_KIND=" + ("SHARED" if shared else "STATIC")], env)
        cache = (self.binary / "CMakeCache.txt").read_text().splitlines()
        library = next(line.split("=", 1)[1] for line in cache
                       if line.startswith("MPV_LIBRARY:"))
        self.assertEqual(library, mpv_path)
        self.run_cmake(["--build", str(self.binary), "--config", "Debug"], env)
        self.run_cmake(["-DCMAKE_INSTALL_PREFIX=" + str(self.install),
                        "-DCMAKE_INSTALL_CONFIG_NAME=Debug", "-P",
                        str(self.binary / "cmake_install.cmake")], env)
        self.assertEqual((self.install / "libmpv-2.dll").read_bytes(), b"primary")
        self.assertEqual((self.install / "libmpv-fallback.dll").read_bytes(), b"fallback")
        if shared:
            self.assertEqual((self.install / "MpvQt.dll").read_bytes(), b"external-mpvqt")
        else:
            self.assertFalse((self.install / "MpvQt.dll").exists())
        args = self.args_log.read_text().splitlines()
        self.assertEqual(Path(args[args.index("--qmldir") + 1]), self.source / "src/ui")

    def run_cmake(self, args, env):
        flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        result = subprocess.run(["cmake", *args], capture_output=True, text=True, env=env,
                                creationflags=flags, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_backslashes_and_spaces(self):
        path = str(self.source / "mpv files/libmpv-2.dll.lib").replace("/", "\\")
        self.check_bundle(path)

    def test_forward_slashes_and_spaces(self):
        self.check_bundle((self.source / "mpv files/libmpv-2.dll.lib").as_posix())

    def test_relative_path_resolves_from_build(self):
        relative = os.path.relpath(self.source / "mpv files/libmpv-2.dll.lib", self.binary)
        self.check_bundle(relative)

    def test_static_mpvqt_does_not_install_a_runtime(self):
        self.check_bundle((self.source / "mpv files/libmpv-2.dll.lib").as_posix(), shared=False)


if __name__ == "__main__":
    unittest.main()
