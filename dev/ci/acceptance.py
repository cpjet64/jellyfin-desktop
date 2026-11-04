"""Validate all native evidence; a status string alone is never proof."""
import json
import os
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

from check_results import check_junit, check_qtest

JOBS = ("windows-x64", "windows-system", "linux-static", "linux-system",
        "macos-static", "macos-system")
SCENARIOS = ("playing", "paused", "held-hook")
STATUSES = ("passed", "failed", "blocked", "not_run")
CORE = ("test_systemcomponent", "test_log", "test_settings", "test_displaymanager",
        "test_log_rotation", "test_chromiumflags", "test_webclient_transport",
        "test_mpv_test_bootstrap_pure")
RUNTIME = ("test_fault_stop", "test_webclient_transport", "test_webclient_fault_guard",
           "test_mpv_test_bootstrap")
STATIC = ("test_mpvqt_event_ownership", "test_mpvqt_player_binding", "test_player_released_state")


def artifact(directory, relative):
    path = directory / relative
    root = directory.resolve(strict=True)
    resolved = path.resolve(strict=True)
    if not resolved.is_relative_to(root) or not resolved.is_file():
        raise ValueError(f"artifact outside namespace or not a file: {relative}")
    if not resolved.stat().st_size:
        raise ValueError(f"empty artifact: {relative}")
    return resolved


def require_case(directory, relative, images=()):
    check_qtest(artifact(directory, relative + "/qtest.txt"))
    for image in images:
        artifact(directory, relative + "/" + image)


def require_render(directory, prefix, before, after, after2):
    for scenario in SCENARIOS:
        images = [] if scenario == "held-hook" else [before, after]
        if scenario == "playing":
            images.append(after2)
        require_case(directory, f"{prefix}/{scenario}", images)


def validate_job(base, job, sha, ref):
    directory = base / job
    if directory.is_symlink() or directory.resolve(strict=True).parent != base.resolve(strict=True):
        raise ValueError(f"{job}: invalid artifact namespace")
    report = json.loads(artifact(directory, "evidence.json").read_text(encoding="utf-8"))
    expected_platform, expected_mode = job.split("-", 1)
    expected_mode = "static" if expected_mode == "x64" else expected_mode
    for key, expected in (("sha", sha), ("ref", ref), ("job", job),
                          ("platform", expected_platform), ("mode", expected_mode)):
        if report.get(key) != expected:
            raise ValueError(f"{job}: incorrect or missing {key}")
    if not str(report.get("job_id", "")).isdecimal():
        raise ValueError(f"{job}: missing job identity")
    for obligation in ("build_core", "offscreen", "desktop"):
        status = report.get(obligation)
        if status not in STATUSES:
            raise ValueError(f"{job}: invalid {obligation} status")
        if status != "passed":
            raise ValueError(f"{job}: {obligation} is {status}")
    check_junit(artifact(directory, "headless-ctest.xml"), CORE)
    required = list(RUNTIME) + (list(STATIC) if expected_mode == "static" else [])
    if expected_platform == "linux":
        required.append("test_mpris_fault")
    check_junit(artifact(directory, "desktop-ctest.xml"), required)
    require_render(directory, "offscreen", "offscreen-before.png", "offscreen-after.png", "offscreen-after2.png")
    require_case(directory, "webengine")
    require_render(directory, "desktop-render", "before.png", "after.png", "after2.png")
    for scenario in SCENARIOS:
        images = [] if scenario == "held-hook" else ["containment-before.png", "containment-after.png"]
        require_case(directory, f"containment/{scenario}", images)
    # No producer exists yet: missing real native-controls proof must fail closed.
    require_case(directory, "native-controls")
    raise ValueError(f"{job}: native-controls receipt verification is not implemented")


def main():
    sha, ref = os.environ["CI_COMMIT_SHA"], os.environ["CI_COMMIT_REF_NAME"]
    errors = []
    for job in JOBS:
        try:
            validate_job(Path("build/ci-evidence"), job, sha, ref)
        except (OSError, ValueError, KeyError, ET.ParseError) as error:
            errors.append(f"{job}: {error}")
    for error in errors:
        print(error, file=sys.stderr)
    return bool(errors)


if __name__ == "__main__":
    sys.exit(main())
