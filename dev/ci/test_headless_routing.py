"""Check headless staging and desktop blockers without launching a GUI."""

from contextlib import ExitStack, redirect_stdout
import io
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.dont_write_bytecode = True
import headless_requirements
import build_manifest
import native


class TestHeadlessRouting(unittest.TestCase):
    def preflight_with(self, found, missing_inputs=()):
        environment = {"CI_MPVQT_MODE": "static", "CI_JOB_ID": "123", "PATH": "/usr/bin"}
        with ExitStack() as stack:
            stack.enter_context(redirect_stdout(io.StringIO()))
            stack.enter_context(patch.dict(native.os.environ, environment, clear=True))
            stack.enter_context(patch.object(native.platform, "system", return_value="Linux"))
            stack.enter_context(patch.object(native.platform, "machine", return_value="x86_64"))
            stack.enter_context(patch.object(native.shutil, "disk_usage", return_value=SimpleNamespace(free=20 * 1024**3)))
            stack.enter_context(patch.object(native, "inventory", return_value=found))
            stack.enter_context(patch.object(native, "unavailable_compilers", return_value=[]))
            stack.enter_context(patch.object(native, "incompatible_tools", return_value=[]))
            stack.enter_context(patch.object(native, "missing_build_inputs", return_value=list(missing_inputs)))
            bootstrap = stack.enter_context(patch.object(native, "bootstrap"))
            try:
                return native.preflight()
            finally:
                bootstrap.assert_not_called()

    def test_missing_display_is_deferred_until_after_core(self):
        found = {name: "/usr/bin/tool" for name in ("cmake", "ctest", "ninja", "node", "pkg-config", "git", "c++")}
        found.update({name: None for name in native.LINUX_DESKTOP_TOOLS})
        system, mode, missing = self.preflight_with(found)
        self.assertEqual((system, mode), ("Linux", "static"))
        self.assertEqual(set(missing), native.LINUX_DESKTOP_TOOLS)

    def test_missing_build_input_prevents_optional_tool_download(self):
        found = {name: "/usr/bin/tool" for name in ("ctest", "ninja", "node", "pkg-config", "git", "c++")}
        found["cmake"] = None
        with self.assertRaisesRegex(RuntimeError, "Qt6"):
            self.preflight_with(found, ["Qt6 development modules missing"])

    def test_core_checks_select_only_explicit_nonvisual_cases(self):
        calls = []
        with redirect_stdout(io.StringIO()), patch.object(native, "command", side_effect=lambda name, args, **kw: calls.append((name, args))):
            native.headless_checks()
        self.assertEqual(len(native.HEADLESS_TESTS), 8)
        names = set(native.HEADLESS_TESTS)
        self.assertTrue({"test_systemcomponent", "test_log", "test_webclient_transport", "test_mpv_test_bootstrap_pure"}.issubset(names))
        self.assertTrue(names.isdisjoint({"test_fault_stop", "test_windowmanager", "test_mpv_test_bootstrap",
                                         "test_mpvqt_event_ownership", "test_webclient_fault_webengine"}))
        ctest = next(args for name, args in calls if name == "headless-ctest")
        self.assertEqual(ctest[ctest.index("-R") + 1], "^(" + "|".join(native.HEADLESS_TESTS) + ")$")
        report = next(args for name, args in calls if name == "headless-ctest-report")
        self.assertEqual({report[i + 1] for i, item in enumerate(report) if item == "--require"}, names)

    def test_headless_evidence_precedes_missing_desktop_failure(self):
        events = []
        with ExitStack() as stack:
            stack.enter_context(patch.dict(native.os.environ, {"CI_EVIDENCE_DIR": "unused"}))
            stack.enter_context(patch.object(native, "update"))
            stack.enter_context(patch.object(build_manifest, "capture"))
            for name in ("configure", "headless_checks", "runtime_ctest", "offscreen"):
                stack.enter_context(patch.object(native, name, side_effect=lambda *a, n=name: events.append(n)))
            stack.enter_context(patch.object(native, "desktop_preflight", side_effect=RuntimeError("BLOCKED")))
            native.run_stages("Darwin", "static", [])
        self.assertEqual(events, ["configure", "headless_checks"])

    def test_all_desktop_gates_still_run_when_available(self):
        events = []
        with ExitStack() as stack:
            stack.enter_context(patch.dict(native.os.environ, {"CI_EVIDENCE_DIR": "unused"}))
            stack.enter_context(patch.object(native, "update"))
            stack.enter_context(patch.object(build_manifest, "capture"))
            for name in ("configure", "headless_checks", "desktop_preflight", "runtime_ctest", "offscreen"):
                stack.enter_context(patch.object(native, name, side_effect=lambda *a, n=name: events.append(n)))
            native.run_stages("Linux", "static", [])
        self.assertEqual(events, ["configure", "headless_checks", "desktop_preflight", "runtime_ctest", "offscreen"])

    def test_missing_linux_display_is_still_a_failure(self):
        with self.assertRaisesRegex(RuntimeError, "xvfb-run"):
            native.desktop_preflight("Linux", ["xvfb-run"])

    def test_mac_session_verification_not_bypassed(self):
        with redirect_stdout(io.StringIO()), patch.object(Path, "lstat", side_effect=FileNotFoundError), \
                patch.object(native, "verify_existing_session") as verify:
            native.desktop_preflight("Darwin", [])
        verify.assert_called_once()

    def test_offscreen_runs_all_scenarios_and_requires_second_playing_frame(self):
        with tempfile.TemporaryDirectory(dir=native.ROOT / "deps") as temp:
            results = Path(temp)
            seen = []
            def fake_command(name, args, **kwargs):
                if name.endswith("-report"):
                    return
                scenario = kwargs["env"]["RECOVERY_SCENARIO"]
                seen.append(scenario)
                case = Path(kwargs["env"]["RECOVERY_EVIDENCE_DIR"])
                (case / "qtest.txt").write_text("result")
                if scenario != "held-hook":
                    for image in ("offscreen-before.png", "offscreen-after.png"):
                        (case / image).write_bytes(b"image")
                if scenario == "playing":
                    (case / "offscreen-after2.png").write_bytes(b"image")
            with patch.object(native, "RESULTS", results), patch.object(native, "record_case"), patch.object(native, "command", side_effect=fake_command):
                native.run_scenarios("test_renderer_loss_offscreen", [], {}, results / "media")
            self.assertEqual(seen, ["playing", "paused", "held-hook"])

    def test_missing_build_inputs_reported_before_bootstrap(self):
        with patch.object(headless_requirements, "known_prefixes", return_value=[]), \
                patch.object(headless_requirements, "pkg_exists", return_value=False):
            missing = headless_requirements.missing_build_inputs("system", {}, None)
        self.assertEqual(len(missing), 3)
        self.assertTrue(any("Qt6" in item for item in missing))
        self.assertTrue(any("mpv development" in item for item in missing))
        self.assertTrue(any("MpvQtConfig" in item for item in missing))


if __name__ == "__main__":
    unittest.main()
