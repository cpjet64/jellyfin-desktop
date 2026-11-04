"""Black-box examples for the native acceptance evidence contract."""
import json
from pathlib import Path
import tempfile
import unittest

import acceptance

ROOT = Path(__file__).resolve().parents[2]
QTEST = "Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted\n"


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
    write(directory / "headless-ctest.xml", junit(acceptance.CORE))
    runtime = list(acceptance.RUNTIME) + (list(acceptance.STATIC) if mode == "static" else [])
    if platform == "linux":
        runtime.append("test_mpris_fault")
    write(directory / "desktop-ctest.xml", junit(runtime))
    for prefix in ("offscreen", "desktop-render", "containment"):
        for scenario in acceptance.SCENARIOS:
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
        for job in acceptance.JOBS:
            fixture(self.base, job)

    def validate(self, job="linux-static"):
        acceptance.validate_job(self.base, job, "abc", "branch")

    def test_complete_six_jobs(self):
        for job in acceptance.JOBS:
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
                with self.assertRaises(ValueError):
                    self.validate()
        write(path, json.dumps(original))

    def test_missing_required_cases_and_failed_junit(self):
        path = self.base / "linux-static/desktop-ctest.xml"
        for text in (junit(acceptance.RUNTIME),
                     '<testsuite><testcase name="test_fault_stop"><failure/></testcase></testsuite>',
                     '<testsuite><testcase name="test_fault_stop"><skipped/></testcase></testsuite>'):
            write(path, text)
            with self.assertRaises(ValueError):
                self.validate()

    def test_missing_failed_skipped_scenario_and_native_controls(self):
        path = self.base / "linux-static/offscreen/held-hook/qtest.txt"
        for text in ("Totals: 3 passed, 1 failed, 0 skipped", "Totals: 3 passed, 0 failed, 1 skipped", ""):
            write(path, text)
            with self.assertRaises(ValueError):
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


if __name__ == "__main__":
    unittest.main()
