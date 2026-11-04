"""Portable checks of job-owned SDK ordering; no downloads or native probes."""
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import native


class LinuxSdkOrderTest(unittest.TestCase):
    def stage(self, fail=None):
        events = []
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp:
            root = Path(tmp)
            sdk = root / "qt"
            sdk.mkdir()
            paths = {"CI_MPV_DEV_DEB": str(root / "dev.deb"), "CI_MPV_INCLUDE_DIR": str(root / "include"),
                     "CI_MPV_LIBRARY": "/usr/lib/libmpv.so.2", "CI_MPV_PINNED_RUNTIME": "1"}
            def step(name, result):
                def run(*args):
                    events.append(name)
                    if name == fail:
                        raise RuntimeError("stage failed")
                    return result
                return run
            modules = {"linux_qt_archive": SimpleNamespace(stage_qt=step("qt", sdk)),
                       "linux_mpv_payload": SimpleNamespace(acquire_mpv=step("mpv", paths)),
                       "linux_sdk_probe": SimpleNamespace(probe_sdk=step("probe", {"environment": {
                           "PKG_CONFIG_PATH": str(sdk / "lib/pkgconfig"), "LD_LIBRARY_PATH": str(sdk / "lib")}}))}
            found = {"cmake": None, "ctest": None, "ninja": "/ninja", "node": "/node"}
            with patch.dict(os.environ, {"CI_JOB_ID": "42", "CI_MPVQT_MODE": "static"}, clear=True), \
                 patch.dict(sys.modules, modules), patch.object(native, "ROOT", root), \
                 patch.object(native.platform, "system", return_value="Linux"), \
                 patch.object(native.platform, "machine", return_value="x86_64"), \
                 patch.object(native.shutil, "disk_usage", return_value=SimpleNamespace(free=20 * 1024**3)), \
                 patch.object(native.shutil, "which", return_value="/tool"), \
                 patch.object(native, "unavailable_compilers", return_value=[]), \
                 patch.object(native, "inventory", return_value=found), \
                 patch.object(native, "incompatible_tools", return_value=[]), \
                 patch.object(native, "fresh_sdk_workspace", return_value=root), \
                 patch.object(native, "bootstrap", side_effect=step("bootstrap", [root / "bin"])), \
                 patch.object(native, "explicit_inputs", side_effect=step("mpv-verify", (root, root))):
                if fail:
                    with self.assertRaisesRegex(RuntimeError, "stage failed"):
                        native.prepare_linux_sdk()
                    self.assertNotIn("CI_QT_PREFIX", os.environ)
                    self.assertNotIn("CI_MPV_DEV_DEB", os.environ)
                else:
                    native.prepare_linux_sdk()
                    self.assertEqual(os.environ["CI_QT_PREFIX"], str(sdk))
                    self.assertEqual(os.environ["CI_MPV_PINNED_RUNTIME"], "1")
            return events

    def test_missing_qt_reaches_preparation_in_order(self):
        self.assertEqual(self.stage(), ["bootstrap", "qt", "mpv", "mpv-verify", "probe"])

    def test_failed_acquisition_or_probe_never_publishes(self):
        for name in ("qt", "mpv", "probe"):
            with self.subTest(step=name):
                self.assertEqual(self.stage(name)[-1], name)

    def test_partial_or_conflicting_inputs_fail_before_acquisition(self):
        for key in ("CI_QT_PREFIX", "CI_MPV_DEV_DEB", "CI_MPV_INCLUDE_DIR", "CI_MPV_LIBRARY", "CI_MPV_PINNED_RUNTIME"):
            with self.subTest(key=key), patch.dict(os.environ, {"CI_MPVQT_MODE": "static", key: "/partial"}, clear=True), \
                 patch.object(native.platform, "system", return_value="Linux"), patch.object(native.platform, "machine", return_value="x86_64"), \
                 patch.object(native, "bootstrap") as bootstrap:
                with self.assertRaisesRegex(RuntimeError, "conflicts"):
                    native.prepare_linux_sdk()
                bootstrap.assert_not_called()

    def test_macos_and_system_do_not_enter_linux_route(self):
        for system, mode in (("Darwin", "static"), ("Linux", "system")):
            with self.subTest(system=system, mode=mode), patch.dict(os.environ, {"CI_MPVQT_MODE": mode}, clear=True), \
                 patch.object(native.platform, "system", return_value=system), patch.object(native.platform, "machine", return_value="x86_64"):
                with self.assertRaisesRegex(RuntimeError, "BLOCKED"):
                    native.prepare_linux_sdk()

    def test_main_orders_preparation_before_full_preflight_and_stages(self):
        events = []
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp, \
             patch.object(native, "BUILD", Path(tmp)), patch.object(native, "RESULTS", Path(tmp) / "results"), \
             patch.dict(os.environ, {"CI_LINUX_SDK_MODE": "job-local"}, clear=True), \
             patch.object(native, "portable_checks", side_effect=lambda: events.append("portable")), \
             patch.object(native, "command", side_effect=lambda name, *a, **k: events.append(name)), \
             patch.object(native, "prepare_linux_sdk", side_effect=lambda: events.append("prepare")), \
             patch.object(native, "preflight", side_effect=lambda prepared_tools: (events.append("full-preflight"), ("Linux", "static", []))[1]) as preflight, \
             patch.object(native, "verify_mpvqt_source", side_effect=lambda: events.append("source")), \
             patch.object(native, "run_stages", side_effect=lambda *_: events.append("configure")):
            native.main()
        preflight.assert_called_once_with(prepared_tools=True)
        self.assertEqual(events[:5], ["portable", "native-prereq-inventory", "prepare", "full-preflight", "source"])
        self.assertEqual(events[-1], "configure")

    def test_preparation_failure_never_reaches_preflight_or_build(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp, \
             patch.object(native, "RESULTS", Path(tmp) / "results"), \
             patch.dict(os.environ, {"CI_LINUX_SDK_MODE": "job-local"}, clear=True), \
             patch.object(native, "portable_checks"), patch.object(native, "command"), \
             patch.object(native, "prepare_linux_sdk", side_effect=RuntimeError("BLOCKED: failed")), \
             patch.object(native, "preflight") as preflight, patch.object(native, "run_stages") as stages:
            with self.assertRaisesRegex(RuntimeError, "BLOCKED: failed"):
                native.main()
            preflight.assert_not_called()
            stages.assert_not_called()

    def test_job_workspace_must_be_fresh_and_owned(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp:
            root = Path(tmp)
            with patch.object(native.os, "getuid", return_value=root.stat().st_uid, create=True):
                workspace = native.fresh_sdk_workspace(root, "42")
                self.assertEqual(workspace, root / "deps" / "ci-42")
                with self.assertRaisesRegex(RuntimeError, "already exists"):
                    native.fresh_sdk_workspace(root, "42")
                with self.assertRaisesRegex(RuntimeError, "numeric"):
                    native.fresh_sdk_workspace(root, "../other")


if __name__ == "__main__":
    unittest.main()
