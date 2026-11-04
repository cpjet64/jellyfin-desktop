"""Record the Linux private-bus active-playback Stop fixture evidence."""
import json


def update_obligation(directory, status, reason):
    path = directory / "evidence.json"
    report = json.loads(path.read_text(encoding="utf-8"))
    report["obligations"]["linux_active_stop"] = {"status": status, "reason": reason}
    path.write_text(json.dumps(report, indent=2), encoding="utf-8")


def run_scenario(build, results, directory, run, scenario):
    raise RuntimeError("BLOCKED: legacy MPRIS offscreen producer violates the render-thread contract")


def run_active_stop(build, results, directory, run):
    raise RuntimeError("BLOCKED: legacy MPRIS offscreen producer violates the render-thread contract")
