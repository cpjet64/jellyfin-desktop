"""Black-box examples for the native acceptance evidence contract."""
import json
from pathlib import Path
import tempfile
import unittest

import acceptance

ROOT = Path(__file__).resolve().parents[2]
QTEST = "Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted\n"
JOBS = ("windows-x64", "windows-system", "linux-static", "linux-system", "macos-static", "macos-system")
CORE = ("test_systemcomponent", "test_log", "test_settings", "test_displaymanager", "test_log_rotation",
        "test_chromiumflags", "test_webclient_transport", "test_mpv_test_bootstrap_pure")
RUNTIME = ("test_fault_stop", "test_webclient_transport", "test_webclient_fault_guard", "test_mpv_test_bootstrap")
STATIC = ("test_mpvqt_event_ownership", "test_mpvqt_player_binding", "test_player_released_state")
SCENARIOS = ("playing", "paused", "held-hook")


def write(path, data="evidence"):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(data, encoding="utf-8")


def junit(names):
    return "<testsuite>" + "".join(f'<testcase name="{name}"/>' for name in names) + "</testsuite>"


def fixture(base, job):
    directory = base / job
    platform, mode = job.split("-", 1)
    mode = "static" if mode == "x64" else mode
    report = dict(sha="abc", ref="branch", job=job, platform=platform, mode=mode,
                  job_id="123", build_core="passed", offscreen="passed", desktop="passed")
    write(directory / "evidence.json", json.dumps(report))
    runtime = list(RUNTIME) + (list(STATIC) if mode == "static" else [])
    if platform == "linux":
        runtime.append("test_mpris_fault")
    write(directory / "headless-ctest.xml", junit(list(CORE) + (runtime if platform == "windows" else [])))
    if platform != "windows":
        write(directory / "runtime-ctest.xml", junit(runtime))
    for prefix in ("offscreen", "desktop-render", "containment"):
        for scenario in SCENARIOS:
            case = directory / prefix / scenario
            write(case / "qtest.txt", QTEST)
            if scenario == "held-hook":
                continue
            images = {"offscreen": ["offscreen-before.png", "offscreen-after.png"],
                      "desktop-render": ["before.png", "after.png"],
                      "containment": ["containment-before.png", "containment-after.png"]}[prefix]
            if scenario == "playing" and prefix != "containment":
                images.append("offscreen-after2.png" if prefix == "offscreen" else "after2.png")
            for image in images:
                write(case / image)
    for case in ("webengine", "native-controls"):
        write(directory / case / "qtest.txt", QTEST)
    return report


class AcceptanceTest(unittest.TestCase):
    def setUp(self):
        (ROOT / "deps").mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / "deps", prefix="acceptance-test-")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        for job in JOBS:
            fixture(self.base, job)

    def validate(self, job="linux-static"):
        acceptance.validate_job(self.base, job, "abc", "branch")

    def test_complete_six_jobs(self):
        self.assertEqual(set(acceptance.JOBS), set(JOBS))
        for job in JOBS:
            with self.assertRaisesRegex(ValueError, "receipt verification is not implemented"):
                self.validate(job)

    def test_provenance_and_all_statuses(self):
        path = self.base / "linux-static/evidence.json"
        original = json.loads(path.read_text())
        for key, value in (("sha", "other"), ("ref", "other"), ("job", "other"),
                           ("platform", "macos"), ("mode", "system"), ("job_id", ""),
                           ("build_core", "failed"), ("offscreen", "blocked"),
                           ("desktop", "not_run"), ("desktop", "unknown")):
            with self.subTest(key=key, value=value):
                changed = dict(original, **{key: value})
                write(path, json.dumps(changed))
                if key == "job_id":
                    diagnostic = "missing job identity"
                elif key in ("build_core", "offscreen", "desktop"):
                    diagnostic = f"invalid {key} status" if value == "unknown" else f"{key} is {value}"
                else:
                    diagnostic = f"incorrect or missing {key}"
                with self.assertRaisesRegex(ValueError, diagnostic):
                    self.validate()
        write(path, json.dumps(original))
        for key in ("sha", "ref", "job", "platform", "mode", "job_id", "build_core", "offscreen", "desktop"):
            changed = dict(original)
            del changed[key]
            write(path, json.dumps(changed))
            diagnostic = ("missing job identity" if key == "job_id" else
                          f"invalid {key} status" if key in ("build_core", "offscreen", "desktop") else
                          f"incorrect or missing {key}")
            with self.subTest(missing=key), self.assertRaisesRegex(ValueError, diagnostic):
                self.validate()
        write(path, json.dumps(original))

    def test_missing_required_cases_and_failed_junit(self):
        path = self.base / "linux-static/runtime-ctest.xml"
        for text, diagnostic in ((junit(RUNTIME), "Missing CTest cases"),
                     ('<testsuite><testcase name="test_fault_stop"><failure/></testcase></testsuite>', "failed, or skipped"),
                     ('<testsuite><testcase name="test_fault_stop"><skipped/></testcase></testsuite>', "failed, or skipped")):
            write(path, text)
            with self.assertRaisesRegex(ValueError, diagnostic):
                self.validate()

    def test_missing_failed_skipped_scenario_and_native_controls(self):
        path = self.base / "linux-static/offscreen/held-hook/qtest.txt"
        for text, diagnostic in (("Totals: 3 passed, 1 failed, 0 skipped", "Incomplete or unsuccessful QTest"),
                                 ("Totals: 3 passed, 0 failed, 1 skipped", "Incomplete or unsuccessful QTest"),
                                 ("", "empty artifact")):
            write(path, text)
            with self.assertRaisesRegex(ValueError, diagnostic):
                self.validate()
        write(path, QTEST)
        (self.base / "linux-static/native-controls/qtest.txt").unlink()
        with self.assertRaises(OSError):
            self.validate()

    def test_missing_artifact_and_namespace(self):
        (self.base / "linux-static/offscreen/playing/offscreen-after2.png").unlink()
        with self.assertRaises(OSError):
            self.validate()
        with self.assertRaises(OSError):
            self.validate("missing-job")

    def test_required_cases_cannot_be_removed_from_contract(self):
        for job in JOBS:
            platform, mode = job.split("-", 1)
            required = list(RUNTIME) + (list(STATIC) if mode in ("x64", "static") else [])
            if platform == "linux":
                required.append("test_mpris_fault")
            for case in CORE:
                path = self.base / job / "headless-ctest.xml"
                headless = list(CORE) + (required if platform == "windows" else [])
                write(path, junit([item for item in headless if item != case]))
                with self.subTest(job=job, case=case), self.assertRaisesRegex(ValueError, f"Missing CTest cases: {case}"):
                    self.validate(job)
                write(path, junit(headless))
            for case in required:
                path = self.base / job / ("headless-ctest.xml" if platform == "windows" else "runtime-ctest.xml")
                names = list(CORE) + required if platform == "windows" else required
                write(path, junit([item for item in names if item != case]))
                with self.subTest(job=job, case=case), self.assertRaisesRegex(ValueError, f"Missing CTest cases: {case}"):
                    self.validate(job)
                write(path, junit(names))

    def test_headless_evidence_passes_its_independent_checks(self):
        for job in JOBS:
            with self.subTest(job=job):
                acceptance.validate_headless(self.base, job, "abc", "branch")

    def test_blocked_desktop_does_not_hide_bad_headless_evidence(self):
        path = self.base / "linux-static/evidence.json"
        report = json.loads(path.read_text())
        report["desktop"] = "blocked"
        report["sha"] = "wrong"
        write(path, json.dumps(report))
        with self.assertRaisesRegex(ValueError, "incorrect or missing sha"):
            self.validate()
        report["sha"] = "abc"
        write(path, json.dumps(report))
        with self.assertRaisesRegex(ValueError, "desktop is blocked"):
            self.validate()

    def test_each_offscreen_scenario_report_is_required(self):
        for scenario in SCENARIOS:
            path = self.base / "linux-static/offscreen" / scenario / "qtest.txt"
            path.unlink()
            with self.subTest(scenario=scenario), self.assertRaisesRegex(OSError, "qtest.txt"):
                self.validate()
            write(path, QTEST)


if __name__ == "__main__":
    unittest.main()
