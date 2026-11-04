"""Test Linux active Stop routing and receipt rejection without a GUI."""
import json
from pathlib import Path
import tempfile
import unittest

import acceptance
import linux_mpris_receipt as producer

ROOT = Path(__file__).resolve().parents[2]
def qtest(scenario):
    return ("PASS   : TestMprisActiveStop::initTestCase()\n"
            f"QINFO  : TestMprisActiveStop::activeStop() MPRIS_STOP_SCENARIO={scenario}\n"
            "PASS   : TestMprisActiveStop::activeStop()\nPASS   : TestMprisActiveStop::cleanupTestCase()\n"
            "Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted\n"
            "********* Finished testing of TestMprisActiveStop *********\n")


class LinuxMprisReceiptTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="mpris-receipt-test-", dir=ROOT)
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.build = self.base / "build"
        (self.build / "tests").mkdir(parents=True)
        (self.build / "tests/test_mpris_active_stop").write_bytes(b"test executable")
        self.results = self.base / "results"
        self.results.mkdir()
        (self.results / "recovery.y4m").write_bytes(b"clip")
        self.report = dict(sha="abc", source_sha="abc", ref="branch", job="linux-static", job_id="1",
                           pipeline_id="2", platform="linux", mode="static", obligations={"linux_active_stop": {}})
        (self.results / "evidence.json").write_text(json.dumps(self.report))
        (self.results / "build-manifest.json").write_text('{"build":"verified"}')

    def run_fixture(self, include_after=True):
        seen = []
        def fake_run(name, args, *, env, timeout):
            scenario = env["RECOVERY_SCENARIO"]
            self.assertEqual(name, "linux-mpris-active-stop-" + scenario)
            seen.append(scenario)
            self.assertEqual(args[:2], ["xvfb-run", "-a"])
            self.assertIn("dbus-run-session", args)
            self.assertEqual(timeout, 90)
            case = Path(env["RECOVERY_EVIDENCE_DIR"])
            (case / "qtest.txt").write_text(qtest(scenario))
            (case / "active-before.png").write_bytes(b"before")
            if include_after:
                (case / "active-after.png").write_bytes(b"after")
        producer.run_active_stop(self.build, self.results, self.results, fake_run)
        self.assertEqual(seen, ["playing", "paused"])

    def test_success_and_identity_image_manifest_validation(self):
        self.run_fixture()
        acceptance.validate_linux_active_stop(self.results, self.report)
        case = self.results / "linux-mpris-active-stop/playing"
        path = case / "case.json"
        original = json.loads(path.read_text())
        for key, value in (("job_id", "other"), ("source_sha", "wrong"), ("launcher", "host-session"),
                           ("exit_code", 1), ("media_sha256", "wrong"), ("build_manifest_sha256", "wrong")):
            path.write_text(json.dumps(dict(original, **{key: value})))
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "incorrect"):
                acceptance.validate_linux_active_stop(self.results, self.report)
        path.write_text(json.dumps(original))
        (case / "active-after.png").write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "incorrect images"):
            acceptance.validate_linux_active_stop(self.results, self.report)

    def test_missing_image_does_not_create_receipt(self):
        with self.assertRaises(OSError):
            self.run_fixture(include_after=False)
        self.assertFalse((self.results / "linux-mpris-active-stop/playing/case.json").exists())

    def test_failed_process_does_not_create_receipt(self):
        def failed(*args, **kwargs):
            raise RuntimeError("native process failed")
        with self.assertRaisesRegex(RuntimeError, "native process failed"):
            producer.run_active_stop(self.build, self.results, self.results, failed)
        self.assertFalse((self.results / "linux-mpris-active-stop/playing/case.json").exists())

    def test_both_scenarios_and_fixture_marker_are_mandatory(self):
        self.run_fixture()
        case = self.results / "linux-mpris-active-stop/paused"
        report = case / "qtest.txt"
        original = report.read_text()
        report.write_text(original.replace("MPRIS_STOP_SCENARIO=paused", "MPRIS_STOP_SCENARIO=playing"))
        with self.assertRaisesRegex(ValueError, "incorrect qtest_sha256"):
            acceptance.validate_linux_active_stop(self.results, self.report)
        with self.assertRaisesRegex(ValueError, "incorrect fixture scenario marker"):
            acceptance.require_mpris_scenario(report, "paused")
        report.write_text(original)
        (case / "case.json").unlink()
        with self.assertRaisesRegex(OSError, "case.json"):
            acceptance.validate_linux_active_stop(self.results, self.report)

    def test_obligation_status_stays_explicit(self):
        producer.update_obligation(self.results, "blocked", "Xvfb unavailable")
        result = json.loads((self.results / "evidence.json").read_text())
        self.assertEqual(result["obligations"]["linux_active_stop"], {"status": "blocked", "reason": "Xvfb unavailable"})


if __name__ == "__main__":
    unittest.main()
