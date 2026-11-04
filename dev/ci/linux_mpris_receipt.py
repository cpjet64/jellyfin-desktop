"""Record the Linux private-bus active-playback Stop fixture evidence."""
import json
import os
from pathlib import Path
import shutil

from acceptance import require_case, require_mpris_scenario
from evidence import digest

PRODUCER = "linux-mpris-active-stop-v1"
IMAGES = ("active-before.png", "active-after.png")


def update_obligation(directory, status, reason):
    path = directory / "evidence.json"
    report = json.loads(path.read_text(encoding="utf-8"))
    report["obligations"]["linux_active_stop"] = {"status": status, "reason": reason}
    path.write_text(json.dumps(report, indent=2), encoding="utf-8")


def run_scenario(build, results, directory, run, scenario):
    case = results / "linux-mpris-active-stop" / scenario
    case.mkdir(parents=True, exist_ok=False)
    executable = build / "tests/test_mpris_active_stop"
    media = results / "recovery.y4m"
    if not media.is_file():
        raise RuntimeError("BLOCKED: generated active-playback clip is missing")
    env = os.environ.copy()
    env.update({"QT_QPA_PLATFORM": "xcb", "QSG_INFO": "1", "RECOVERY_MEDIA": str(media),
                "RECOVERY_EVIDENCE_DIR": str(case), "RECOVERY_SCENARIO": scenario})
    prefix = ["xvfb-run", "-a", "-s", "-screen 0 1280x800x24 +extension GLX", "dbus-run-session", "--"]
    run("linux-mpris-active-stop-" + scenario, prefix + [str(executable), "-o", str(case / "qtest.txt") + ",txt"],
        env=env, timeout=90)
    require_case(case.parent, case.name, IMAGES, ("TestMprisActiveStop", "activeStop"))
    require_mpris_scenario(case / "qtest.txt", scenario)
    report = json.loads((directory / "evidence.json").read_text(encoding="utf-8"))
    identity = {key: report[key] for key in ("sha", "ref", "job", "job_id", "pipeline_id", "platform", "mode", "source_sha")}
    record = dict(identity, producer=PRODUCER, scenario=scenario, launcher="xvfb-private-dbus-v1", exit_code=0,
                  executable_sha256=digest(executable), qtest_sha256=digest(case / "qtest.txt"),
                  images={name: digest(case / name) for name in IMAGES}, media_sha256=digest(media),
                  build_manifest_sha256=digest(directory / "build-manifest.json"))
    binaries = directory / "desktop-executables"
    binaries.mkdir(parents=True, exist_ok=True)
    shutil.copy2(executable, binaries / PRODUCER)
    (case / "case.json").write_text(json.dumps(record, indent=2), encoding="utf-8")


def run_active_stop(build, results, directory, run):
    for scenario in ("playing", "paused"):
        run_scenario(build, results, directory, run, scenario)
