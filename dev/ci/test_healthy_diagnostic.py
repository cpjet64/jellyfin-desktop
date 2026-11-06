"""Exercise diagnostic routing and failure propagation without native launches."""

from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import healthy_diagnostic as diagnostic
import native


class DiagnosticRoutingTest(unittest.TestCase):
    def setUp(self):
        proof = native.ROOT / "deps" / "w1061-test-output"
        proof.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=proof)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        self.results = self.root / "results"
        self.build.mkdir()
        self.results.mkdir()
        self.calls = []

    def invoke(self, system="Linux", fail=None, report="valid"):
        def command(name, args, **kwargs):
            self.calls.append((name, args, kwargs))
            if name == fail:
                raise RuntimeError("injected " + name)
            if name == "healthy-diagnostic-configure":
                Path(args[args.index("-B") + 1]).mkdir()
            if name == "healthy-diagnostic-ctest" and report != "missing":
                names = diagnostic.DIAGNOSTIC_TESTS if report == "valid" else ("unexpected",)
                cases = "".join('<testcase status="run" name="' + name + '"/>' for name in names)
                Path(args[args.index("--output-junit") + 1]).write_text(
                    '<testsuite tests="' + str(len(names)) + '" failures="0">' + cases + "</testsuite>")

        with patch.object(diagnostic, "require_prerequisites", return_value=None):
            diagnostic.run(system, self.root, self.build, self.results, command, {})

    def test_unix_static_and_system_routes(self):
        for system in ("Linux", "Darwin"):
            for mode in ("static", "system"):
                with self.subTest(system=system, mode=mode), \
                        patch.object(native, "configure"), patch.object(native, "headless_checks"), \
                        patch.object(native, "run_healthy_diagnostic") as run, \
                        patch("build_manifest.capture"), patch.object(native, "update"), \
                        patch.object(native, "desktop_preflight", side_effect=RuntimeError("blocked")), \
                        patch.object(native, "update_obligation"), \
                        patch.dict(native.os.environ, {"CI_EVIDENCE_DIR": str(self.root)}):
                    if system == "Linux":
                        with self.assertRaisesRegex(RuntimeError, "blocked"):
                            native.run_stages(system, mode, [])
                    else:
                        native.run_stages(system, mode, [])
                    self.assertEqual(run.call_count, int(mode == "static"))

    def test_explicit_selection_and_owned_cache(self):
        self.invoke()
        names = [item[0] for item in self.calls]
        self.assertEqual(names, ["healthy-diagnostic-configure", "healthy-diagnostic-build", "healthy-diagnostic-ctest"])
        configure, build, ctest = [item[1] for item in self.calls]
        self.assertIn("-DHEALTHY_RESTART_DIAGNOSTIC_ONLY=ON", configure)
        self.assertIn("-DPLAYER_INTENT_DIAGNOSTIC_ONLY=ON", configure)
        self.assertIn("-DUSE_STATIC_MPVQT=ON", configure)
        self.assertIn("-DMPVQT_EXTERNAL_EVENTS=ON", configure)
        self.assertEqual(build[build.index("--target") + 1:], list(diagnostic.DIAGNOSTIC_TESTS))
        self.assertEqual(ctest[ctest.index("-R") + 1], "^(" + "|".join(diagnostic.DIAGNOSTIC_TESTS) + ")$")
        self.assertIn("--no-tests=error", ctest)
        self.assertIn("healthy_graph_capture_lifetime", diagnostic.DIAGNOSTIC_TESTS)
        self.assertTrue(Path(self.calls[-1][2]["env"]["JELLYFIN_TEST_OWNED_ROOT"]).is_dir())
        self.assertNotIn("healthy_mpvqt_event_ownership", diagnostic.DIAGNOSTIC_TESTS)

    def test_missing_prerequisite_fails_before_configure(self):
        with patch.object(diagnostic.shutil, "which", return_value=None), \
                patch.object(diagnostic, "incompatible_tools", return_value=[]), \
                patch.object(diagnostic, "unavailable_compilers", return_value=[]), \
                patch.object(diagnostic, "explicit_inputs", return_value=None), \
                patch.object(diagnostic, "missing_build_inputs", return_value=[]):
            with self.assertRaisesRegex(RuntimeError, "prerequisites"):
                diagnostic.run("Linux", self.root, self.build, self.results, lambda *a, **k: self.fail(), {})

    def test_empty_or_duplicate_selection_fails_before_configure(self):
        for selection in ((), ("same", "same")):
            with self.subTest(selection=selection), patch.object(diagnostic, "DIAGNOSTIC_TESTS", selection):
                with self.assertRaisesRegex(RuntimeError, "selection"):
                    self.invoke()
                self.assertFalse(self.calls)

    def test_configure_build_and_ctest_failures_propagate(self):
        for stage in ("healthy-diagnostic-configure", "healthy-diagnostic-build", "healthy-diagnostic-ctest"):
            with self.subTest(stage=stage):
                self.setUp()
                with self.assertRaisesRegex(RuntimeError, "injected"):
                    self.invoke(fail=stage)
                self.assertEqual(self.calls[-1][0], stage)

    def test_missing_or_wrong_junit_is_rejected(self):
        for report in ("missing", "wrong"):
            with self.subTest(report=report):
                self.setUp()
                with self.assertRaises(RuntimeError):
                    self.invoke(report=report)

    def test_junit_rejects_malformed_failed_skipped_and_duplicate(self):
        path = self.results / "report.xml"
        for contents in ("<broken", "<testsuite/>",
                         '<testsuite tests="1" failures="0"><testcase name="one" status="run"><failure/></testcase></testsuite>',
                         '<testsuite tests="1" failures="0"><testcase name="one" status="run"><skipped/></testcase></testsuite>',
                         '<testsuite tests="1" failures="0"><testcase name="one" status="notrun"/></testsuite>',
                         '<testsuite tests="1" failures="1"><testcase name="one" status="run"/></testsuite>',
                         '<testsuite tests="1" failures="0" errors="1"><testcase name="one" status="run"/></testsuite>',
                         '<testsuite tests="1" failures="0" disabled="1"><testcase name="one" status="run"/></testsuite>',
                         '<testsuite tests="1" failures="0" skipped="1"><testcase name="one" status="run"/></testsuite>',
                         '<testsuite tests="2" failures="0"><testcase name="one" status="run"/></testsuite>',
                         '<testsuite tests="1" failures="0"><testcase name="one" status="run"/><group><testcase name="extra" status="run"/></group></testsuite>',
                         '<testsuite tests="1" failures="0"><testcase name="one" status="run"/><testsuite/></testsuite>',
                         '<testsuite tests="1" failures="0"><testcase name="one" status="run"/><testcase name="one" status="run"/></testsuite>'):
            with self.subTest(contents=contents):
                path.write_text(contents)
                with self.assertRaises(RuntimeError):
                    diagnostic.verify_report(path, ("one",))

    def test_old_cmake_and_missing_build_inputs_are_rejected(self):
        with patch.object(diagnostic.shutil, "which", return_value="tool"), \
                patch.object(diagnostic, "incompatible_tools", return_value=["cmake"]), \
                patch.object(diagnostic, "unavailable_compilers", return_value=[]), \
                patch.object(diagnostic, "explicit_inputs", return_value=None), \
                patch.object(diagnostic, "missing_build_inputs", return_value=["Qt6"]):
            with self.assertRaisesRegex(RuntimeError, "cmake.*Qt6"):
                diagnostic.require_prerequisites("Linux", self.root, {})

    def test_existing_output_is_not_reused(self):
        (self.results / "healthy-diagnostic-ctest.xml").write_text("stale")
        with self.assertRaisesRegex(RuntimeError, "already exists"):
            self.invoke()
        self.assertFalse(self.calls)


if __name__ == "__main__":
    unittest.main()
