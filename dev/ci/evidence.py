"""Record native job progress and retain partial evidence on failure."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

JOBS = {"windows-x64": ("windows", "static"), "windows-system": ("windows", "system"),
        "linux-static": ("linux", "static"), "linux-system": ("linux", "system"),
        "macos-static": ("macos", "static"), "macos-system": ("macos", "system")}
PHASES = ("build_core", "offscreen", "desktop")
ROOT = Path(__file__).resolve().parents[2]


def update(directory, phase, status, reason):
    path = directory / "evidence.json"
    report = json.loads(path.read_text(encoding="utf-8"))
    if phase not in PHASES or status not in ("not_run", "passed", "failed", "blocked"):
        raise ValueError("invalid evidence transition")
    report[phase] = status
    report[phase + "_reason"] = reason
    report["active_phase"] = phase if status == "not_run" else ""
    path.write_text(json.dumps(report, indent=2), encoding="utf-8")


def run_job():
    job = os.environ["CI_JOB_NAME"]
    platform, mode = JOBS[job]
    directory = ROOT / "build" / "ci-evidence" / job
    directory.mkdir(parents=True, exist_ok=False)
    report = dict(sha=os.environ["CI_COMMIT_SHA"], ref=os.environ["CI_COMMIT_REF_NAME"],
                  job=job, platform=platform, mode=mode, job_id=os.environ["CI_JOB_ID"],
                  active_phase="build_core", build_core="not_run", offscreen="not_run",
                  desktop="blocked", desktop_reason="Desktop receipt verification is not implemented")
    (directory / "evidence.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    env = os.environ.copy()
    env["CI_EVIDENCE_DIR"] = str(directory)
    command = (["powershell.exe", "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy",
                "Bypass", "-File", "dev/windows/ci.ps1"] if platform == "windows"
               else [sys.executable, "dev/ci/native.py"])
    try:
        options = {"creationflags": subprocess.CREATE_NO_WINDOW} if platform == "windows" else {}
        code = subprocess.call(command, cwd=ROOT, env=env, **options)
    except OSError as error:
        print(f"Unable to start native job: {error}", file=sys.stderr)
        code = 1
    report = json.loads((directory / "evidence.json").read_text(encoding="utf-8"))
    if code and report.get("active_phase"):
        update(directory, report["active_phase"], "failed", f"Native process exited {code}")
    elif code and report.get("offscreen") == "passed":
        update(directory, "build_core", "failed", f"Post-test native process exited {code}")
    results = ROOT / "build" / "ci-results"
    if results.is_dir():
        shutil.copytree(results, directory, dirs_exist_ok=True)
        offscreen = results / "test_renderer_loss_offscreen"
        if offscreen.is_dir():
            shutil.copytree(offscreen, directory / "offscreen", dirs_exist_ok=True)
    return code


if __name__ == "__main__":
    if len(sys.argv) == 1:
        sys.exit(run_job())
    if len(sys.argv) != 4:
        sys.exit("usage: evidence.py PHASE STATUS REASON")
    update(Path(os.environ["CI_EVIDENCE_DIR"]), *sys.argv[1:])
