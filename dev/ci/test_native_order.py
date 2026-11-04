"""Check that the mandatory CMake test cannot precede tool preparation."""
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import native


class NativeOrderTest(unittest.TestCase):
    def exercise(self, available, blocked=False, system="Linux", mode="static", node_available=True,
                 smoke_failed=False):
        events = []
        found = {key: "/tool" for key in ("cmake", "ctest", "ninja", "node", "pkg-config", "git", "c++")}
        found["cmake"] = "/tool" if available else None
        found["node"] = "/tool" if node_available else None
        def command(name, args, **kwargs):
            events.append(name)
            if name == "windows-smoke-result-tests" and smoke_failed:
                raise RuntimeError("smoke regression failed")
        def bootstrap(requested, destination):
            events.append("bootstrap")
            expected = ({"cmake"} if not available else set()) | ({"node"} if not node_available else set())
            self.assertEqual(requested, expected)
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
            elif smoke_failed:
                with self.assertRaisesRegex(RuntimeError, "smoke regression failed"):
                    native.main()
            else:
                native.main()
        return events

    def test_bootstrap_before_required_cmake_check(self):
        self.assertEqual(self.exercise(False), ["portable", "native-prereq-inventory", "bootstrap", "windows-smoke-result-tests",
                                                "explicit-mpv-cmake-tests", "stages"])

    def test_existing_cmake_still_runs_required_check(self):
        self.assertEqual(self.exercise(True), ["portable", "native-prereq-inventory",
                                               "windows-smoke-result-tests",
                                               "explicit-mpv-cmake-tests", "stages"])

    def test_failed_bootstrap_blocks_check_and_stages(self):
        self.assertEqual(self.exercise(False, blocked=True), ["portable", "native-prereq-inventory", "bootstrap"])

    def test_macos_system_still_runs_required_check(self):
        self.assertEqual(self.exercise(True, system="Darwin", mode="system"),
                         ["portable", "native-prereq-inventory", "windows-smoke-result-tests", "explicit-mpv-cmake-tests", "stages"])

    def test_bootstrapped_node_precedes_required_smoke_test(self):
        self.assertEqual(self.exercise(True, node_available=False),
                         ["portable", "native-prereq-inventory", "bootstrap", "windows-smoke-result-tests",
                          "explicit-mpv-cmake-tests", "stages"])

    def test_failed_node_bootstrap_blocks_smoke_test(self):
        self.assertEqual(self.exercise(True, blocked=True, node_available=False),
                         ["portable", "native-prereq-inventory", "bootstrap"])

    def test_failed_smoke_test_blocks_cmake_check_and_stages(self):
        self.assertEqual(self.exercise(True, smoke_failed=True),
                         ["portable", "native-prereq-inventory", "windows-smoke-result-tests"])


if __name__ == "__main__":
    unittest.main()
