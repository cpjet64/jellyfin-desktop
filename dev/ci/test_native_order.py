"""Check that the mandatory CMake test cannot precede tool preparation."""
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import native


class NativeOrderTest(unittest.TestCase):
    def exercise(self, available, blocked=False, system="Linux", mode="static"):
        events = []
        found = {key: "/tool" for key in ("cmake", "ctest", "ninja", "node", "pkg-config", "git", "c++")}
        found["cmake"] = "/tool" if available else None
        def command(name, args, **kwargs):
            events.append(name)
        def bootstrap(requested, destination):
            events.append("bootstrap")
            self.assertEqual(requested, {"cmake"})
            if blocked:
                raise RuntimeError("BLOCKED: bootstrap failed")
            return [Path("/job/bin")]
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp, \
             patch.object(native, "BUILD", Path(tmp)), patch.object(native, "RESULTS", Path(tmp) / "results"), \
             patch.dict(os.environ, {"CI_JOB_ID": "42", "CI_MPVQT_MODE": mode}, clear=True), \
             patch.object(native.platform, "system", return_value=system), \
             patch.object(native.platform, "machine", return_value="x86_64"), \
             patch.object(native.shutil, "disk_usage", return_value=SimpleNamespace(free=20 * 1024**3)), \
             patch.object(native, "inventory", return_value=found), \
             patch.object(native, "unavailable_compilers", return_value=[]), \
             patch.object(native, "incompatible_tools", return_value=[]), \
             patch.object(native, "explicit_inputs", return_value=None), \
             patch.object(native, "missing_build_inputs", return_value=[]), \
             patch.object(native, "bootstrap", side_effect=bootstrap), \
             patch.object(native.shutil, "which", return_value="/job/bin/cmake"), \
             patch.object(native, "portable_checks", side_effect=lambda: events.append("portable")), \
             patch.object(native, "command", side_effect=command), \
             patch.object(native, "run_stages", side_effect=lambda *_: events.append("stages")):
            if blocked:
                with self.assertRaisesRegex(RuntimeError, "bootstrap failed"):
                    native.main()
            else:
                native.main()
        return events

    def test_bootstrap_before_required_cmake_check(self):
        self.assertEqual(self.exercise(False), ["portable", "native-prereq-inventory", "bootstrap",
                                                "explicit-mpv-cmake-tests", "stages"])

    def test_existing_cmake_still_runs_required_check(self):
        self.assertEqual(self.exercise(True), ["portable", "native-prereq-inventory",
                                               "explicit-mpv-cmake-tests", "stages"])

    def test_failed_bootstrap_blocks_check_and_stages(self):
        self.assertEqual(self.exercise(False, blocked=True), ["portable", "native-prereq-inventory", "bootstrap"])

    def test_macos_system_still_runs_required_check(self):
        self.assertEqual(self.exercise(True, system="Darwin", mode="system"),
                         ["portable", "native-prereq-inventory", "explicit-mpv-cmake-tests", "stages"])


if __name__ == "__main__":
    unittest.main()
