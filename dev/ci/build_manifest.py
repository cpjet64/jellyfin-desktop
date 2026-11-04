"""Capture only nonsecret, explicitly selected CMake build identity fields."""
import json
import os
from pathlib import Path

from evidence import digest

KEYS = ("CMAKE_HOME_DIRECTORY", "USE_STATIC_MPVQT", "MPVQT_EXTERNAL_EVENTS", "Qt6_DIR",
        "MPV_INCLUDE_DIR", "MPV_LIBRARY", "MPV_LIBRARY_mpv", "PC_MPV_VERSION")


def found(value):
    return bool(value and value != "NOTFOUND" and not value.endswith("-NOTFOUND"))


def read_cache(path):
    values = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        key, separator, value = line.partition("=")
        name, _, _type = key.partition(":")
        if separator and name in KEYS:
            values[name] = value
    return values


def capture(root, directory, executable):
    report = json.loads((directory / "evidence.json").read_text(encoding="utf-8"))
    cache = read_cache(root / "build/CMakeCache.txt")
    if Path(cache.get("CMAKE_HOME_DIRECTORY", "")).resolve() != root.resolve():
        raise ValueError("CMake source directory does not match the checkout")
    expected = "ON" if report["mode"] == "static" else "OFF"
    if any(cache.get(name) != expected for name in ("USE_STATIC_MPVQT", "MPVQT_EXTERNAL_EVENTS")):
        raise ValueError("CMake mpvqt configuration does not match the CI mode")
    if not found(cache.get("Qt6_DIR")) or not found(cache.get("MPV_INCLUDE_DIR")) or not any(found(cache.get(key)) for key in ("MPV_LIBRARY", "MPV_LIBRARY_mpv")):
        raise ValueError("CMake cache lacks Qt or mpv discovery evidence")
    manifest = dict(source_sha=report["source_sha"], source_dir=report["source_dir"], mpvqt_sha=report["mpvqt_sha"], mode=report["mode"],
                    executable_sha256=digest(executable), cmake=cache)
    if report["mode"] == "static":
        manifest["c1_patch_sha256"] = digest(root / "build/patched-mpvqt/0001-event-ownership.patch")
        manifest["c1_controller_sha256"] = digest(root / "build/patched-mpvqt/src/mpvcontroller.cpp")
    (directory / "build-manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")


if __name__ == "__main__":
    capture(Path(__file__).resolve().parents[2], Path(os.environ["CI_EVIDENCE_DIR"]),
            Path(os.environ["CI_EVIDENCE_EXECUTABLE"]))
