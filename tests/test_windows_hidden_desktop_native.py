"""Run four bounded, task-owned hidden-desktop process containment cases."""
import argparse
import os
from pathlib import Path
import re
import subprocess


def run_case(helper, fixture, directory, mode):
    case = directory / mode
    case.mkdir()
    env = os.environ.copy()
    env["JELLYFIN_HELPER_FIXTURE_MODE"] = mode
    result = subprocess.run([str(helper), str(fixture), str(case), "--test-executable"],
                            env=env, capture_output=True, text=True, timeout=85,
                            creationflags=subprocess.CREATE_NO_WINDOW, check=False)
    log = result.stdout + "\n--- stderr ---\n" + result.stderr
    (case / "console.log").write_text(log, encoding="utf-8")
    expected_exit = 0 if mode == "zero" else 7
    assert result.returncode == expected_exit, (mode, result.returncode, log)
    expected = {
        "zero": ("app wait 0 exit 0", "terminationRequested false", "overall pass"),
        "nonzero": ("app wait 0 exit 42", "terminationRequested false", "overall fail"),
        "timeout": ("app wait 258", "terminationRequested true", "overall fail"),
        "child": ("fixture child ready", "app wait 0 exit 0", "terminationRequested true", "overall fail"),
    }[mode]
    for marker in (*expected, "activeAfterKnown true activeAfter 0 activeAfterError 0"):
        assert marker in log, (mode, marker, log)
    if mode in ("child", "timeout"):
        assert re.search(r"(?m)^activeBeforeKnown true activeBefore [1-9]\d* activeBeforeError 0\s*$", log), log
        assert "terminationRequested true terminationSucceeded true terminationError 0" in log, log
    else:
        assert "activeBeforeKnown true activeBefore 0 activeBeforeError 0" in log, log
        assert "terminationRequested false terminationSucceeded false terminationError 0" in log, log


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("helper", type=Path)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if not all(path.is_absolute() for path in (args.helper, args.fixture, args.output)):
        parser.error("all paths must be absolute")
    args.output.mkdir()
    for mode in ("zero", "nonzero", "timeout", "child"):
        run_case(args.helper, args.fixture, args.output, mode)
        print(mode, "passed", flush=True)


if __name__ == "__main__":
    main()
