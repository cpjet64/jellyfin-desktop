"""Required post-bootstrap test of real CMake library selection."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ExplicitMpvCMakeTest(unittest.TestCase):
    def test_actual_findmpv_ignores_stale_pkg_selection_only_in_explicit_mode(self):
        self.assertIsNotNone(shutil.which("cmake"), "cmake is required for this check")
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp:
            root = Path(tmp)
            module = (Path(__file__).resolve().parents[2] / "CMakeModules/FindMPV.cmake").as_posix()
            include, expected, other = (root / name for name in ("include", "mpv.lib", "other.lib"))
            include.mkdir()
            expected.touch()
            other.touch()
            script = root / "check.cmake"
            script.write_text('''set(WIN32 FALSE)
set(PKG_CONFIG_FOUND TRUE)
macro(pkg_check_modules)
  set(PC_MPV_LIBRARIES other)
endmacro()
set(PC_MPV_LIBRARIES other)
set(MPV_INCLUDE_DIR "@INCLUDE@")
set(MPV_LIBRARY_mpv "@EXPECTED@")
set(MPV_LIBRARY_other "@OTHER@")
include("@MODULE@")
if(NOT MPV_LIBRARY STREQUAL WANT)
  message(FATAL_ERROR "unexpected effective library: ${MPV_LIBRARY}")
endif()
'''.replace("@INCLUDE@", include.as_posix()).replace("@EXPECTED@", expected.as_posix())
               .replace("@OTHER@", other.as_posix()).replace("@MODULE@", module))
            for explicit, want in (("ON", expected), ("OFF", other)):
                subprocess.run(["cmake", "-DJELLYFIN_EXPLICIT_MPV=" + explicit, "-DWANT=" + want.as_posix(),
                                "-P", str(script)], check=True, capture_output=True, text=True, timeout=30)


if __name__ == "__main__":
    unittest.main()
