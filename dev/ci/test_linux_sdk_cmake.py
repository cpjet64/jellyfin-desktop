"""Run the real CMake probe helper only after the tool bootstrap."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class LinuxSdkCMakeTest(unittest.TestCase):
    def test_graphics_route_and_tool_path_with_real_cmake(self):
        module = (Path(__file__).parent / "linux_sdk_probe/probe_checks.cmake").resolve().as_posix()
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp:
            root = Path(tmp)
            qt = root / "qt"
            qt.mkdir()
            local, other = qt / "moc", root / "other"
            local.touch()
            other.touch()
            script = root / "probe.cmake"
            script.write_text(f'include("{module}")\nset(QTROOT "{qt.as_posix()}")\n'
                              'select_graphics("${GL}" "gl-lib" "${GLES}" "gles-lib")\n'
                              'if(NOT PROBE_GRAPHICS_DEFINE STREQUAL EXPECT)\nmessage(FATAL_ERROR "wrong route")\nendif()\n'
                              'require_qt_path("${TOOL}" "tool")\n')
            def invoke(gl, gles, expected, tool):
                return subprocess.run(["cmake", "-DGL=" + gl, "-DGLES=" + gles, "-DEXPECT=" + expected,
                                       "-DTOOL=" + tool.as_posix(), "-P", str(script)], capture_output=True, timeout=30,
                                      creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            self.assertEqual(invoke("ON", "OFF", "PROBE_OPENGL", local).returncode, 0)
            self.assertEqual(invoke("OFF", "ON", "PROBE_GLES2", local).returncode, 0)
            self.assertNotEqual(invoke("OFF", "OFF", "unused", local).returncode, 0)
            self.assertNotEqual(invoke("ON", "OFF", "PROBE_OPENGL", other).returncode, 0)


if __name__ == "__main__":
    unittest.main()
