"""Black-box examples for the native acceptance evidence contract."""
import json
import hashlib
from pathlib import Path
import tempfile
import unittest

import acceptance

ROOT = Path(__file__).resolve().parents[2]
def qtest(class_name, case, scenario="playing"):
    return (f"PASS   : {class_name}::initTestCase()\nQINFO  : {class_name}::{case}() RECOVERY_SCENARIO={scenario}\n"
            f"QINFO  : {class_name}::{case}() RECOVERY_CAPABILITIES context=valid surface=valid current=true render_readback=valid\n"
            f"PASS   : {class_name}::{case}()\nPASS   : {class_name}::cleanupTestCase()\n"
            f"Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted\n********* Finished testing of {class_name} *********\n")

QTEST = qtest("TestRendererLossOffscreen", "actualPlayback")
JOBS = ("windows-x64", "windows-system", "linux-static", "linux-system", "macos-static", "macos-system")
CORE = ("test_systemcomponent", "test_log", "test_settings", "test_displaymanager", "test_log_rotation",
        "test_chromiumflags", "test_webclient_transport", "test_mpv_test_bootstrap_pure", "test_smtc_admission_model", "test_c2a_hold_gate", "test_c2_error_terminal", "test_c2_fault_quiescence_policy", "test_c2_synthetic_service")
RUNTIME = ("test_fault_stop", "test_webclient_transport", "test_webclient_fault_guard", "test_mpv_test_bootstrap", "test_windowmanager")
MPVQT_SHA = "2ebbd38bf65d01be9c55d673ecd43c8bb9a6fc8e"
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
                  job_id="123", pipeline_id="456", source_sha="abc", source_dir="/checkout", mpvqt_sha=MPVQT_SHA,
                  build_core="passed", offscreen="passed", desktop="passed")
    names = ["webengine", "windowed_playback", "containment"]
    names += {"windows": ["windows_geometry", "windows_callback"],
              "linux": ["linux_mpris_fault", "linux_active_stop"], "macos": ["macos_media_callback"]}[platform]
    report["obligations"] = {name: {"status": "blocked", "reason": "not available"} for name in names}
    write(directory / "evidence.json", json.dumps(report))
    runtime = list(RUNTIME) + (list(STATIC) if mode == "static" else [])
    if platform == "linux":
        runtime.append("test_mpris_fault")
    if platform == "windows":
        runtime.append("test_windows_smtc_lifetime")
    write(directory / "headless-ctest.xml", junit(list(CORE) + (runtime if platform == "windows" else [])))
    if platform != "windows":
        write(directory / "runtime-ctest.xml", junit(runtime))
    write(directory / "offscreen-executable", "binary")
    options = "ON" if mode == "static" else "OFF"
    cache = dict(CMAKE_HOME_DIRECTORY="/checkout", USE_STATIC_MPVQT=options, MPVQT_EXTERNAL_EVENTS=options,
                 Qt6_DIR="/qt", MPV_INCLUDE_DIR="/mpv/include", MPV_LIBRARY="/mpv/lib")
    manifest = dict(source_sha="abc", source_dir="/checkout", mpvqt_sha=MPVQT_SHA, mode=mode, cmake=cache,
                    executable_sha256=hashlib.sha256(b"binary").hexdigest())
    if mode == "static":
        manifest.update(c1_patch_sha256="a" * 64, c1_controller_sha256="b" * 64)
    write(directory / "build-manifest.json", json.dumps(manifest))
    for prefix in ("offscreen", "desktop-render", "containment"):
        for scenario in SCENARIOS:
            case = directory / prefix / scenario
            write(case / "qtest.txt", qtest("TestRendererLossOffscreen", "actualPlayback", scenario))
            images = [] if scenario == "held-hook" else {"offscreen": ["offscreen-before.png", "offscreen-after.png"],
                      "desktop-render": ["before.png", "after.png"],
                      "containment": ["containment-before.png", "containment-after.png"]}[prefix]
            if scenario == "playing" and prefix != "containment":
                images.append("offscreen-after2.png" if prefix == "offscreen" else "after2.png")
            for image in images:
                write(case / image)
            if prefix == "offscreen":
                metadata = dict(sha="abc", ref="branch", job=job, platform=platform, mode=mode,
                                job_id="123", pipeline_id="456", scenario=scenario, producer="offscreen-render-v1",
                                executable_sha256=hashlib.sha256(b"binary").hexdigest(),
                                qtest_sha256=hashlib.sha256((case / "qtest.txt").read_bytes()).hexdigest(),
                                images={image: hashlib.sha256((case / image).read_bytes()).hexdigest() for image in images})
                write(case / "case.json", json.dumps(metadata))
    for case in ("webengine", "native-controls"):
        write(directory / case / "qtest.txt", QTEST)
    return report


class AcceptanceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=ROOT, prefix="acceptance-test-")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        for job in JOBS:
            fixture(self.base, job)

    def validate(self, job="linux-static"):
        acceptance.validate_job(self.base, job, "abc", "branch", "456")

    def validate_historical_format(self):
        directory = self.base / "linux-static"
        report = json.loads((directory / "evidence.json").read_text())
        acceptance.require_render(directory, "offscreen", "offscreen-before.png", "offscreen-after.png",
                                  "offscreen-after2.png", ("TestRendererLossOffscreen", "actualPlayback"))
        acceptance.check_offscreen_metadata(directory, report)

    def test_complete_six_jobs(self):
        self.assertEqual(set(acceptance.JOBS), set(JOBS))
        for job in JOBS:
            with self.assertRaisesRegex(ValueError, "legacy offscreen-render-v1 receipts are invalidated"):
                self.validate(job)

    def test_callback_guard_is_required_for_each_platform(self):
        for job in JOBS:
            path = self.base / job / "headless-ctest.xml"
            path.write_text(path.read_text().replace('<testcase name="test_c2a_hold_gate"/>', ''), encoding="utf-8")
            with self.subTest(job=job), self.assertRaisesRegex(ValueError, "Missing CTest cases: test_c2a_hold_gate"):
                self.validate(job)

    def test_error_terminal_is_required_for_each_platform(self):
        for job in JOBS:
            path = self.base / job / "headless-ctest.xml"
            path.write_text(path.read_text().replace('<testcase name="test_c2_error_terminal"/>', ''), encoding="utf-8")
            with self.subTest(job=job), self.assertRaisesRegex(ValueError, "Missing CTest cases: test_c2_error_terminal"):
                self.validate(job)

    def test_quiescence_policy_is_required_for_each_platform(self):
        for job in JOBS:
            path = self.base / job / "headless-ctest.xml"
            path.write_text(path.read_text().replace('<testcase name="test_c2_fault_quiescence_policy"/>', ''), encoding="utf-8")
            with self.subTest(job=job), self.assertRaisesRegex(ValueError, "Missing CTest cases: test_c2_fault_quiescence_policy"):
                self.validate(job)

    def test_synthetic_service_is_required_for_each_platform(self):
        for job in JOBS:
            path = self.base / job / "headless-ctest.xml"
            path.write_text(path.read_text().replace('<testcase name="test_c2_synthetic_service"/>', ''), encoding="utf-8")
            with self.subTest(job=job), self.assertRaisesRegex(ValueError, "Missing CTest cases: test_c2_synthetic_service"):
                self.validate(job)

    def test_provenance_and_all_statuses(self):
        path = self.base / "linux-static/evidence.json"
        original = json.loads(path.read_text())
        for key, value in (("sha", "other"), ("ref", "other"), ("job", "other"),
                           ("platform", "macos"), ("mode", "system"), ("pipeline_id", "999"), ("job_id", ""),
                           ("build_core", "failed"), ("offscreen", "blocked")):
            with self.subTest(key=key, value=value):
                changed = dict(original, **{key: value})
                write(path, json.dumps(changed))
                if key == "job_id":
                    diagnostic = "missing job_id identity"
                elif key in ("build_core", "offscreen", "desktop"):
                    diagnostic = f"invalid {key} status" if value == "unknown" else f"{key} is {value}"
                else:
                    diagnostic = f"incorrect or missing {key}"
                with self.assertRaisesRegex(ValueError, diagnostic):
                    self.validate()
        write(path, json.dumps(original))
        for key in ("sha", "ref", "job", "platform", "mode", "pipeline_id", "job_id", "build_core", "offscreen"):
            changed = dict(original)
            del changed[key]
            write(path, json.dumps(changed))
            diagnostic = ("missing job_id identity" if key == "job_id" else
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
                self.validate_historical_format()
        write(path, qtest("TestRendererLossOffscreen", "actualPlayback", "held-hook"))

    def test_unsupported_callback_claim_fails_closed(self):
        path = self.base / "windows-x64/evidence.json"
        report = json.loads(path.read_text())
        report["obligations"]["windows_callback"] = {"status": "passed", "producer": "invented"}
        errors = acceptance.validate_desktop(path.parent, report)
        self.assertTrue(any("windows_callback: passed claim has no supported receipt producer" in error for error in errors))

    def test_scenario_identity_and_provenance_are_required(self):
        case = self.base / "linux-static/offscreen/playing"
        path = case / "case.json"
        original = json.loads(path.read_text())
        for key, value in (("scenario", "paused"), ("producer", "unknown"), ("pipeline_id", "999"),
                           ("sha", "wrong"), ("job_id", "999"), ("executable_sha256", "wrong")):
            write(path, json.dumps(dict(original, **{key: value})))
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, f"incorrect scenario evidence {key}"):
                self.validate_historical_format()
        write(path, json.dumps(original))
        report = case / "qtest.txt"
        write(report, qtest("WrongClass", "actualPlayback"))
        with self.assertRaisesRegex(ValueError, "Missing or duplicate named QTest PASS"):
            self.validate_historical_format()

    def test_offscreen_image_tampering_is_rejected(self):
        path = self.base / "linux-static/offscreen/playing/offscreen-after2.png"
        write(path, "different image")
        with self.assertRaisesRegex(ValueError, "incorrect scenario evidence images"):
            self.validate_historical_format()

    def test_windowed_receipt_binds_exact_images_and_content(self):
        directory = self.base / "windows-x64"
        report = json.loads((directory / "evidence.json").read_text())
        case = directory / "desktop-render/playing"
        write(case / "qtest.txt", qtest("TestRendererLossPlayback", "actualPlayback", "playing"))
        binary = directory / "desktop-executables/windowed-render-v1"
        write(binary, "desktop binary")
        names = ("before.png", "after.png", "after2.png")
        record = {key: report[key] for key in ("sha", "ref", "job", "job_id", "pipeline_id", "platform", "mode")}
        record.update(producer="windowed-render-v1", scenario="playing",
                      executable_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                      qtest_sha256=hashlib.sha256((case / "qtest.txt").read_bytes()).hexdigest(),
                      images={name: hashlib.sha256((case / name).read_bytes()).hexdigest() for name in names})
        receipt = case / "case.json"
        def check():
            acceptance.validate_named_receipt(directory, report, "desktop-render/playing", "windowed-render-v1",
                                               "playing", ("TestRendererLossPlayback", "actualPlayback"), names)
        write(receipt, json.dumps(record))
        check()
        for images in ({}, {**record["images"], "extra.png": "0" * 64},
                       {name: digest for name, digest in record["images"].items() if name != "after.png"}):
            write(receipt, json.dumps(dict(record, images=images)))
            with self.subTest(images=images), self.assertRaisesRegex(ValueError, "incorrect images"):
                check()
        write(receipt, json.dumps(record))
        write(case / "after.png", "changed image bytes")
        with self.assertRaisesRegex(ValueError, "incorrect images"):
            check()

    def test_wrong_footer_extra_case_and_total_are_rejected(self):
        path = self.base / "linux-static/offscreen/playing/qtest.txt"
        original = path.read_text(encoding="utf-8")
        examples = ((original.replace("Finished testing of TestRendererLossOffscreen", "Finished testing of WrongClass"),
                     "Incorrect QTest footer"),
                    (original.replace("Totals:", "PASS   : OtherClass::other()\nTotals:").replace("Totals: 3 passed", "Totals: 4 passed"),
                     "Unexpected named QTest PASS"),
                    (original.replace("Totals: 3 passed", "Totals: 4 passed"), "Incomplete or unsuccessful QTest"))
        for text, expected in examples:
            write(path, text)
            with self.subTest(expected=expected), self.assertRaisesRegex(ValueError, expected):
                self.validate_historical_format()

    def test_build_manifest_rejects_missing_malformed_and_mismatched_provenance(self):
        path = self.base / "linux-static/build-manifest.json"
        original = json.loads(path.read_text())
        for field, value, diagnostic in (("source_sha", "wrong", "incorrect source_sha"),
                                          ("source_dir", "/other", "incorrect source_dir"),
                                          ("mode", "system", "incorrect mode"),
                                          ("executable_sha256", "wrong", "incorrect executable hash"),
                                          ("cmake", {}, "incorrect CMake source directory")):
            write(path, json.dumps(dict(original, **{field: value})))
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, diagnostic):
                self.validate()
        write(path, "{")
        with self.assertRaises(json.JSONDecodeError):
            self.validate()
        path.unlink()
        with self.assertRaisesRegex(OSError, "build-manifest.json"):
            self.validate()

    def test_geometry_requires_all_three_clean_launches_and_snapshots(self):
        directory = self.base / "windows-x64"
        report = json.loads((directory / "evidence.json").read_text())
        record = {key: report[key] for key in ("sha", "ref", "job", "job_id", "pipeline_id", "platform", "mode")}
        record.update(producer="windows-geometry-v1", sha256={})
        for name in ("executable", "helper"):
            path = directory / "geometry" / name
            write(path, name)
            record[name + "_sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
        markers = "\n".join("window actions " + item for item in
                            ("F11 enter passed", "F11 restore passed", "Alt+Return enter passed",
                             "Alt+Return restore passed", "normal bounds passed"))
        for number in range(1, 4):
            prefix = f"geometry/geometry-{number}"
            data = {"result.json": json.dumps({"case": f"geometry-{number}", "helperExit": 0,
                                               "failures": [], "bounds": "1 2 3 4", "observation": {"exitRequested": True}}),
                    "storage.json": json.dumps({"sections": {"state": {"XPosition": 1}}}),
                    "console.log": "window bounds 1 2 3 4\napp wait 0 exit 0\n"
                    "waitResult 0 waitError 0 exitKnown true exitError 0\n"
                    "activeBeforeKnown true activeBefore 0 activeBeforeError 0\n"
                    "terminationRequested false terminationSucceeded false terminationError 0\n"
                    "activeAfterKnown true activeAfter 0 activeAfterError 0 overall pass\n"
                    "forcedCleanup false; job active processes 0\n" + markers}
            for name, text in data.items():
                path = directory / prefix / name
                write(path, text)
                record["sha256"][f"{prefix}/{name}"] = hashlib.sha256(path.read_bytes()).hexdigest()
        acceptance.validate_geometry(directory, report, record)
        (directory / "geometry/geometry-2/storage.json").unlink()
        with self.assertRaisesRegex(OSError, "storage.json"):
            acceptance.validate_geometry(directory, report, record)

    def test_missing_artifact_and_namespace(self):
        (self.base / "linux-static/offscreen/playing/offscreen-after2.png").unlink()
        with self.assertRaises(OSError):
            self.validate_historical_format()
        with self.assertRaises(OSError):
            self.validate("missing-job")

    def test_required_cases_cannot_be_removed_from_contract(self):
        for job in JOBS:
            platform, mode = job.split("-", 1)
            required = list(RUNTIME) + (list(STATIC) if mode in ("x64", "static") else [])
            if platform == "linux":
                required.append("test_mpris_fault")
            if platform == "windows":
                required.append("test_windows_smtc_lifetime")
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
            with self.subTest(job=job), self.assertRaisesRegex(ValueError, "legacy offscreen-render-v1 receipts are invalidated"):
                acceptance.validate_headless(self.base, job, "abc", "branch", "456")

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
        with self.assertRaisesRegex(ValueError, "legacy offscreen-render-v1 receipts are invalidated"):
            self.validate()

    def test_each_offscreen_scenario_report_is_required(self):
        for scenario in SCENARIOS:
            path = self.base / "linux-static/offscreen" / scenario / "qtest.txt"
            path.unlink()
            with self.subTest(scenario=scenario), self.assertRaisesRegex(OSError, "qtest.txt"):
                self.validate_historical_format()
            write(path, qtest("TestRendererLossOffscreen", "actualPlayback", scenario))


if __name__ == "__main__":
    unittest.main()
