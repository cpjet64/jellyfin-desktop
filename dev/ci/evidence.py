"""Record native job progress and retain partial evidence on failure."""
import json
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
from desktop_policy import blocked_obligations

JOBS = {"windows-x64": ("windows", "static"), "windows-system": ("windows", "system"),
        "linux-static": ("linux", "static"), "linux-system": ("linux", "system"),
        "macos-static": ("macos", "static"), "macos-system": ("macos", "system")}
PHASES = ("build_core", "offscreen", "desktop")
ROOT = Path(__file__).resolve().parents[2]


def verify_source(root, expected):
    def git(*args):
        result = subprocess.run(["git", *args], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                text=True, timeout=60, check=True)
        return result.stdout
    actual = git("rev-parse", "HEAD").strip()
    if actual != expected:
        raise ValueError("source HEAD does not match CI_COMMIT_SHA")
    if git("status", "--porcelain", "--untracked-files=no", "--ignore-submodules=none").strip():
        raise ValueError("tracked source or submodule worktree is dirty")
    submodules = git("submodule", "status", "--recursive").splitlines()
    if any(not line.startswith(" ") for line in submodules):
        raise ValueError("submodule is missing, conflicted, or differs from its recorded commit")
    mpvqt = git("-C", "external/mpvqt", "rev-parse", "HEAD").strip()
    if mpvqt != "2ebbd38bf65d01be9c55d673ecd43c8bb9a6fc8e":
        raise ValueError("unexpected MpvQt source revision")
    return actual, mpvqt


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def record_case(directory, executable, scenario):
    if scenario not in ("playing", "paused", "held-hook"):
        raise ValueError("unknown scenario")
    from acceptance import require_case, require_offscreen_capability
    require_case(Path(directory).parent, Path(directory).name, expected=("TestRendererLossOffscreen", "actualPlayback"), scenario=scenario)
    require_offscreen_capability(Path(directory) / "qtest.txt")
    job = os.environ["CI_JOB_NAME"]
    platform, mode = JOBS[job]
    record = dict(producer="offscreen-render-v1", scenario=scenario, job=job,
                  platform=platform, mode=mode, job_id=os.environ["CI_JOB_ID"],
                  pipeline_id=os.environ["CI_PIPELINE_ID"], sha=os.environ["CI_COMMIT_SHA"],
                  ref=os.environ["CI_COMMIT_REF_NAME"], executable_sha256=digest(executable),
                  qtest_sha256=digest(Path(directory) / "qtest.txt"))
    images = [] if scenario == "held-hook" else ["offscreen-before.png", "offscreen-after.png"]
    if scenario == "playing":
        images.append("offscreen-after2.png")
    record["images"] = {name: digest(Path(directory) / name) for name in images}
    (Path(directory) / "case.json").write_text(json.dumps(record, indent=2), encoding="utf-8")


def update(directory, phase, status, reason):
    path = directory / "evidence.json"
    report = json.loads(path.read_text(encoding="utf-8"))
    if phase not in PHASES or status not in ("not_run", "passed", "failed", "blocked"):
        raise ValueError("invalid evidence transition")
    report[phase] = status
    report[phase + "_reason"] = reason
    report["active_phase"] = phase if status == "not_run" else ""
    path.write_text(json.dumps(report, indent=2), encoding="utf-8")


def run_process(command, cwd, env, platform, log):
    options = {"creationflags": subprocess.CREATE_NO_WINDOW} if platform == "windows" else {}
    process = subprocess.Popen(command, cwd=cwd, env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, errors="replace", bufsize=1, **options)
    try:
        with log.open("w", encoding="utf-8") as output:
            for line in process.stdout:
                output.write(line)
                output.flush()
                print(line, end="", flush=True)
        return process.wait()
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        if process.stdout:
            process.stdout.close()


def run_job():
    job = os.environ["CI_JOB_NAME"]
    platform, mode = JOBS[job]
    directory = ROOT / "build" / "ci-evidence" / job
    directory.mkdir(parents=True, exist_ok=False)
    report = dict(sha=os.environ["CI_COMMIT_SHA"], ref=os.environ["CI_COMMIT_REF_NAME"],
                  job=job, platform=platform, mode=mode, job_id=os.environ["CI_JOB_ID"], pipeline_id=os.environ["CI_PIPELINE_ID"],
                  active_phase="build_core", build_core="not_run", offscreen="not_run",
                  desktop="blocked", desktop_reason="Desktop obligations not run by the headless job",
                  obligations=blocked_obligations(platform))
    (directory / "evidence.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    try:
        report["source_sha"], report["mpvqt_sha"] = verify_source(ROOT, report["sha"])
        report["source_dir"] = str(ROOT.resolve())
        (directory / "evidence.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        update(directory, "build_core", "failed", str(error))
        print(f"Source verification failed: {error}", file=sys.stderr)
        return 1
    env = os.environ.copy()
    env["CI_EVIDENCE_DIR"] = str(directory)
    command = (["powershell.exe", "-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy",
                "Bypass", "-File", "dev/windows/ci.ps1"] if platform == "windows"
               else [sys.executable, "dev/ci/native.py"])
    try:
        code = run_process(command, ROOT, env, platform, directory / "native-process.log")
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
    executable = ROOT / "build/tests" / ("test_renderer_loss_offscreen.exe" if platform == "windows" else "test_renderer_loss_offscreen")
    if executable.is_file() and (directory / "offscreen").is_dir():
        shutil.copy2(executable, directory / "offscreen-executable")
    return code


if __name__ == "__main__":
    if len(sys.argv) == 1:
        sys.exit(run_job())
    if len(sys.argv) == 5 and sys.argv[1] == "record-case":
        record_case(*sys.argv[2:])
        sys.exit(0)
    if len(sys.argv) != 4:
        sys.exit("usage: evidence.py PHASE STATUS REASON")
    update(Path(os.environ["CI_EVIDENCE_DIR"]), *sys.argv[1:])
