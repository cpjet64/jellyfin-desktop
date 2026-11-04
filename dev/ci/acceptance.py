"""Validate all native evidence; a status string alone is never proof."""
import json
import os
import hashlib
import re
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

from check_results import check_junit, check_qtest
from desktop_policy import COMMON, PLATFORM
from build_manifest import found

JOBS = ("windows-x64", "windows-system", "linux-static", "linux-system",
        "macos-static", "macos-system")
SCENARIOS = ("playing", "paused", "held-hook")
STATUSES = ("passed", "failed", "blocked", "not_run")
CORE = ("test_systemcomponent", "test_log", "test_settings", "test_displaymanager",
        "test_log_rotation", "test_chromiumflags", "test_webclient_transport",
        "test_mpv_test_bootstrap_pure", "test_smtc_admission_model")
RUNTIME = ("test_fault_stop", "test_webclient_transport", "test_webclient_fault_guard",
           "test_mpv_test_bootstrap", "test_windowmanager")
STATIC = ("test_mpvqt_event_ownership", "test_mpvqt_player_binding", "test_player_released_state")


def clean_windows_helper(log):
    log = log.replace("\r\n", "\n")
    markers = (
        r"^app wait 0 exit 0$",
        r"^waitResult 0 waitError 0 exitKnown true exitError 0$",
        r"^activeBeforeKnown true activeBefore 0 activeBeforeError 0$",
        r"^terminationRequested false terminationSucceeded false terminationError 0$",
        r"^activeAfterKnown true activeAfter 0 activeAfterError 0 overall pass$",
        r"^forcedCleanup false; job active processes 0$",
    )
    prefixes = ("app wait ", "waitResult ", "activeBeforeKnown ", "terminationRequested ",
                "activeAfterKnown ", "forcedCleanup ")
    return (all(len(re.findall(marker, log, re.MULTILINE)) == 1 for marker in markers) and
            all(sum(line.startswith(prefix) for line in log.splitlines()) == 1 for prefix in prefixes))


def artifact(directory, relative):
    path = directory / relative
    root = directory.resolve(strict=True)
    resolved = path.resolve(strict=True)
    if not resolved.is_relative_to(root) or not resolved.is_file():
        raise ValueError(f"artifact outside namespace or not a file: {relative}")
    if not resolved.stat().st_size:
        raise ValueError(f"empty artifact: {relative}")
    return resolved


def require_case(directory, relative, images=(), expected=None, scenario=None):
    path = artifact(directory, relative + "/qtest.txt")
    check_qtest(path)
    if expected:
        text = path.read_text(encoding="utf-8", errors="replace")
        for case in ("initTestCase", expected[1], "cleanupTestCase"):
            line = rf"(?m)^PASS\s*:\s*{re.escape(expected[0])}::{re.escape(case)}\(\)\s*$"
            if len(re.findall(line, text)) != 1:
                raise ValueError(f"Missing or duplicate named QTest PASS: {expected[0]}::{case}")
        all_passes = re.findall(r"(?m)^PASS\s*:\s*([^\r\n]+)$", text)
        required_passes = {f"{expected[0]}::{case}()" for case in ("initTestCase", expected[1], "cleanupTestCase")}
        if len(all_passes) != 3 or {item.strip() for item in all_passes} != required_passes:
            raise ValueError(f"Unexpected named QTest PASS case: {expected[0]}")
        if not text.rstrip().endswith(f"********* Finished testing of {expected[0]} *********"):
            raise ValueError(f"Incorrect QTest footer: {expected[0]}")
        if scenario:
            marker = rf"(?m)^QINFO\s*:\s*{re.escape(expected[0])}::{re.escape(expected[1])}\(\) RECOVERY_SCENARIO=([^\s]+)\s*$"
            if re.findall(marker, text) != [scenario]:
                raise ValueError(f"Missing or incorrect fixture scenario marker: {scenario}")
    for image in images:
        artifact(directory, relative + "/" + image)


def require_render(directory, prefix, before, after, after2, expected=None):
    for scenario in SCENARIOS:
        images = [] if scenario == "held-hook" else [before, after]
        if scenario == "playing":
            images.append(after2)
        require_case(directory, f"{prefix}/{scenario}", images, expected, scenario if expected else None)


def require_offscreen_capability(path):
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    capability = (r"(?m)^QINFO\s*:\s*TestRendererLossOffscreen::actualPlayback\(\) "
                  r"RECOVERY_CAPABILITIES context=valid surface=valid current=true render_readback=valid\s*$")
    if len(re.findall(capability, text)) != 1:
        raise ValueError("missing offscreen capability observation")


def check_offscreen_metadata(directory, report):
    executable = artifact(directory, "offscreen-executable")
    binary_hash = hashlib.sha256(executable.read_bytes()).hexdigest()
    for scenario in SCENARIOS:
        prefix = f"offscreen/{scenario}"
        require_offscreen_capability(artifact(directory, prefix + "/qtest.txt"))
        case = json.loads(artifact(directory, prefix + "/case.json").read_text(encoding="utf-8"))
        required = {key: report[key] for key in ("sha", "ref", "job", "platform", "mode", "job_id", "pipeline_id")}
        required.update(producer="offscreen-render-v1", scenario=scenario, executable_sha256=binary_hash,
                        qtest_sha256=hashlib.sha256(artifact(directory, prefix + "/qtest.txt").read_bytes()).hexdigest())
        images = [] if scenario == "held-hook" else ["offscreen-before.png", "offscreen-after.png"]
        if scenario == "playing":
            images.append("offscreen-after2.png")
        required["images"] = {name: hashlib.sha256(artifact(directory, prefix + "/" + name).read_bytes()).hexdigest() for name in images}
        for key, value in required.items():
            if case.get(key) != value:
                raise ValueError(f"{scenario}: incorrect scenario evidence {key}")


def validate_build_manifest(directory, report):
    manifest = json.loads(artifact(directory, "build-manifest.json").read_text(encoding="utf-8"))
    for key in ("source_sha", "source_dir", "mpvqt_sha", "mode"):
        if not report.get(key) or manifest.get(key) != report[key]:
            raise ValueError(f"build manifest: incorrect {key}")
    if report["source_sha"] != report["sha"]:
        raise ValueError("build manifest: source SHA differs from pipeline")
    if report["mpvqt_sha"] != "2ebbd38bf65d01be9c55d673ecd43c8bb9a6fc8e":
        raise ValueError("build manifest: unexpected MpvQt revision")
    if report["mode"] == "static" and any(not re.fullmatch(r"[0-9a-f]{64}", str(manifest.get(key, "")))
                                          for key in ("c1_patch_sha256", "c1_controller_sha256")):
        raise ValueError("build manifest: missing patched MpvQt identifiers")
    cache = manifest.get("cmake")
    if not isinstance(cache, dict):
        raise ValueError("build manifest: missing CMake evidence")
    normalize = lambda value: value.replace("\\", "/").rstrip("/").casefold() if report["platform"] == "windows" else value.rstrip("/")
    if normalize(cache.get("CMAKE_HOME_DIRECTORY", "")) != normalize(report["source_dir"]):
        raise ValueError("build manifest: incorrect CMake source directory")
    expected = "ON" if report["mode"] == "static" else "OFF"
    if any(cache.get(key) != expected for key in ("USE_STATIC_MPVQT", "MPVQT_EXTERNAL_EVENTS")):
        raise ValueError("build manifest: incorrect CMake mode")
    if not found(cache.get("Qt6_DIR")) or not found(cache.get("MPV_INCLUDE_DIR")) or not any(found(cache.get(key)) for key in ("MPV_LIBRARY", "MPV_LIBRARY_mpv")):
        raise ValueError("build manifest: missing Qt or mpv evidence")
    binary = artifact(directory, "offscreen-executable")
    if manifest.get("executable_sha256") != hashlib.sha256(binary.read_bytes()).hexdigest():
        raise ValueError("build manifest: incorrect executable hash")


def validate_headless(base, job, sha, ref, pipeline_id):
    directory = base / job
    if directory.is_symlink() or directory.resolve(strict=True).parent != base.resolve(strict=True):
        raise ValueError(f"{job}: invalid artifact namespace")
    report = json.loads(artifact(directory, "evidence.json").read_text(encoding="utf-8"))
    expected_platform, expected_mode = job.split("-", 1)
    expected_mode = "static" if expected_mode == "x64" else expected_mode
    for key, expected in (("sha", sha), ("ref", ref), ("job", job),
                          ("platform", expected_platform), ("mode", expected_mode), ("pipeline_id", pipeline_id)):
        if report.get(key) != expected:
            raise ValueError(f"{job}: incorrect or missing {key}")
    for key in ("job_id", "pipeline_id"):
        if not str(report.get(key, "")).isdecimal():
            raise ValueError(f"{job}: missing {key} identity")
    for obligation in ("build_core", "offscreen"):
        status = report.get(obligation)
        if status not in STATUSES:
            raise ValueError(f"{job}: invalid {obligation} status")
        if status != "passed":
            raise ValueError(f"{job}: {obligation} is {status}")
    required = list(RUNTIME) + (list(STATIC) if expected_mode == "static" else [])
    if expected_platform == "linux":
        required.append("test_mpris_fault")
    if expected_platform == "windows":
        required.append("test_windows_smtc_lifetime")
        check_junit(artifact(directory, "headless-ctest.xml"), list(CORE) + required)
    else:
        check_junit(artifact(directory, "headless-ctest.xml"), CORE)
        check_junit(artifact(directory, "runtime-ctest.xml"), required)
    validate_build_manifest(directory, report)
    raise ValueError("legacy offscreen-render-v1 receipts are invalidated by the render-thread contract")


def validate_geometry(directory, report, record):
    if record.get("producer") != "windows-geometry-v1":
        raise ValueError("windows_geometry: unsupported producer")
    for key in ("sha", "ref", "job", "job_id", "pipeline_id", "platform", "mode"):
        if record.get(key) != report.get(key):
            raise ValueError(f"windows_geometry: incorrect {key}")
    for name in ("executable", "helper"):
        path = artifact(directory, "geometry/" + name)
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if record.get(name + "_sha256") != digest:
            raise ValueError(f"windows_geometry: incorrect {name} hash")
    bounds, storage = [], []
    for number in range(1, 4):
        prefix = f"geometry/geometry-{number}"
        result = json.loads(artifact(directory, prefix + "/result.json").read_text(encoding="utf-8"))
        snapshot = json.loads(artifact(directory, prefix + "/storage.json").read_text(encoding="utf-8"))
        log = artifact(directory, prefix + "/console.log").read_text(encoding="utf-8", errors="replace")
        for name in ("result.json", "storage.json", "console.log"):
            relative = prefix + "/" + name
            digest = hashlib.sha256(artifact(directory, relative).read_bytes()).hexdigest()
            if record.get("sha256", {}).get(relative) != digest:
                raise ValueError(f"windows_geometry: missing or mismatched hash for {relative}")
        if result.get("case") != f"geometry-{number}" or result.get("helperExit") != 0 or result.get("failures") != []:
            raise ValueError(f"windows_geometry: failed geometry-{number}")
        observation = result.get("observation", {})
        if observation.get("error") or observation.get("actionError") or observation.get("exitRequested") is not True:
            raise ValueError(f"windows_geometry: incomplete observation geometry-{number}")
        matches = re.findall(r"(?m)^window bounds (-?\d+ -?\d+ -?\d+ -?\d+)\s*$", log)
        if len(matches) != 1 or matches[0] != result.get("bounds"):
            raise ValueError(f"windows_geometry: missing or inconsistent bounds geometry-{number}")
        if not clean_windows_helper(log):
            raise ValueError(f"windows_geometry: unclean geometry-{number}")
        if number == 1:
            for marker in ("F11 enter passed", "F11 restore passed", "Alt+Return enter passed",
                           "Alt+Return restore passed", "normal bounds passed"):
                if "window actions " + marker not in log:
                    raise ValueError(f"windows_geometry: missing {marker}")
        bounds.append(result.get("bounds"))
        storage.append(snapshot.get("sections", {}).get("state"))
    if not bounds[0] or len(set(bounds)) != 1 or not storage[0] or any(item != storage[0] for item in storage):
        raise ValueError("windows_geometry: bounds or storage snapshots differ")


def validate_named_receipt(directory, report, prefix, producer, scenario, expected, images=(), scenario_marker=True):
    require_case(directory, prefix, images, expected, scenario if scenario != "single" and scenario_marker else None)
    record = json.loads(artifact(directory, prefix + "/case.json").read_text(encoding="utf-8"))
    binary = artifact(directory, "desktop-executables/" + producer)
    required = {key: report[key] for key in ("sha", "ref", "job", "job_id", "pipeline_id", "platform", "mode")}
    required.update(producer=producer, scenario=scenario, executable_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                    qtest_sha256=hashlib.sha256(artifact(directory, prefix + "/qtest.txt").read_bytes()).hexdigest())
    required["images"] = {name: hashlib.sha256(artifact(directory, prefix + "/" + name).read_bytes()).hexdigest()
                          for name in images}
    for key, expected_value in required.items():
        if record.get(key) != expected_value:
            raise ValueError(f"{prefix}: incorrect {key}")


def validate_windowed(directory, report, name):
    if name == "webengine":
        return validate_named_receipt(directory, report, "webengine", "webengine-v1", "single",
                                      ("TestWebClientFaultWebEngine", "channelCannotReattach"))
    playback = name == "windowed_playback"
    prefix = "desktop-render" if playback else "containment"
    producer = "windowed-render-v1" if playback else "containment-v1"
    expected = ("TestRendererLossPlayback", "actualPlayback") if playback else ("TestWebClientContainment", "containment")
    for scenario in SCENARIOS:
        images = [] if scenario == "held-hook" else (["before.png", "after.png"] if playback else
                                                     ["containment-before.png", "containment-after.png"])
        if playback and scenario == "playing":
            images.append("after2.png")
        validate_named_receipt(directory, report, f"{prefix}/{scenario}", producer, scenario, expected, images)


def require_mpris_scenario(path, scenario):
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    marker = r"(?m)^QINFO\s*:\s*TestMprisActiveStop::activeStop\(\) MPRIS_STOP_SCENARIO=([^\s]+)\s*$"
    if re.findall(marker, text) != [scenario]:
        raise ValueError(f"linux_active_stop: missing or incorrect fixture scenario marker: {scenario}")


def validate_linux_active_stop(directory, report):
    raise ValueError("legacy MPRIS offscreen receipts are invalidated by the render-thread contract")


def validate_desktop(directory, report):
    platform = report.get("platform")
    if platform not in PLATFORM:
        return ["desktop: unsupported platform"]
    entries = report.get("obligations")
    if not isinstance(entries, dict):
        return ["desktop: missing obligations"]
    errors = []
    for name in COMMON + PLATFORM[platform]:
        record = entries.get(name, {})
        status = record.get("status") if isinstance(record, dict) else None
        if status not in STATUSES:
            errors.append(f"{name}: invalid or missing status")
        elif status != "passed":
            reason = record.get("reason")
            errors.append(f"{name}: {status}: {reason or 'missing reason'}")
        elif name in COMMON or name in ("windows_geometry", "linux_mpris_fault", "linux_active_stop"):
            try:
                if name == "windows_geometry":
                    validate_geometry(directory, report, record)
                elif name == "linux_mpris_fault":
                    validate_named_receipt(directory, report, "linux-mpris-fault", "linux-mpris-fault-v1", "single",
                                           ("TestMprisFault", "faultRejectsSeek"))
                elif name == "linux_active_stop":
                    validate_linux_active_stop(directory, report)
                else:
                    validate_windowed(directory, report, name)
            except (OSError, ValueError, KeyError) as error:
                errors.append(str(error))
        else:
            errors.append(f"{name}: passed claim has no supported receipt producer")
    return errors


def validate_job(base, job, sha, ref, pipeline_id):
    report, directory = validate_headless(base, job, sha, ref, pipeline_id)
    status = report.get("desktop")
    if status not in STATUSES:
        raise ValueError(f"{job}: invalid desktop status")
    errors = validate_desktop(directory, report)
    if status != "passed":
        errors.insert(0, f"{job}: desktop is {status}")
    if errors:
        raise ValueError("; ".join(errors))


def main():
    sha, ref, pipeline_id = os.environ["CI_COMMIT_SHA"], os.environ["CI_COMMIT_REF_NAME"], os.environ["CI_PIPELINE_ID"]
    errors = []
    for job in JOBS:
        directory = Path("build/ci-evidence") / job
        try:
            validate_headless(Path("build/ci-evidence"), job, sha, ref, pipeline_id)
        except (OSError, ValueError, KeyError, ET.ParseError) as error:
            errors.append(f"{job}: {error}")
        try:
            report = json.loads(artifact(directory, "evidence.json").read_text(encoding="utf-8"))
            if report.get("desktop") != "passed":
                errors.append(f"{job}: desktop is {report.get('desktop', 'missing')}")
            errors.extend(f"{job}: {error}" for error in validate_desktop(directory, report))
        except (OSError, ValueError, KeyError) as error:
            errors.append(f"{job}: desktop evidence unavailable: {error}")
    for error in errors:
        print(error, file=sys.stderr)
    return bool(errors)


if __name__ == "__main__":
    sys.exit(main())
