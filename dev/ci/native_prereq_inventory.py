"""Read-only native runner prerequisite proposal; never install or update."""

import json
import contextlib
import io
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time

from macos_session import verify_existing_session

PACKAGES = ("libmpv-dev", "libmpv2", "xvfb", "mesa-utils")
TOOLS = ("cc", "c++", "cmake", "ninja", "pkg-config")
SAFE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9.+:~_-]*$")


def run(args, timeout=60):
    allowed = {"apt-cache", "apt-get", "ldd", "sw_vers", "arch", "xcode-select", "xcrun", "pkgutil", "stat"}
    if not args or args[0] not in allowed:
        raise ValueError("command not allowed")
    if args[0] == "apt-get" and (len(args) < 5 or args[1:4] !=
                                  ["--simulate", "--no-install-recommends", "install"]):
        raise ValueError("apt-get requires simulated non-recommends install")
    if args[0] == "apt-cache" and (len(args) < 3 or args[1] not in ("policy", "show")):
        raise ValueError("apt-cache accepts policy or show only")
    if args[0] in ("apt-cache", "apt-get"):
        operands = args[2:] if args[0] == "apt-cache" else args[4:]
        if any(not all(SAFE.fullmatch(part) for part in operand.split("=")) or
               operand.count("=") > 1 for operand in operands):
            raise ValueError("invalid package operand")
    if args[0] == "pkgutil" and args[1:] != ["--pkg-info", "com.apple.pkg.CLTools_Executables"]:
        raise ValueError("unexpected package metadata request")
    if args[0] == "stat" and args[1:] != ["-f", "%u", "/dev/console"]:
        raise ValueError("unexpected console metadata request")
    fixed = {"ldd": ["--version"], "sw_vers": [], "arch": [], "xcode-select": ["-p"]}
    if args[0] in fixed and args[1:] != fixed[args[0]]:
        raise ValueError("unexpected metadata arguments")
    if args[0] == "xcrun" and args[1:] not in (["--show-sdk-path"], ["--show-sdk-version"]):
        raise ValueError("unexpected SDK metadata request")
    try:
        result = subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=timeout, env={**os.environ, "LC_ALL": "C"})
        return {"command": args, "exit": result.returncode, "output": result.stdout}
    except (OSError, subprocess.TimeoutExpired) as error:
        return {"command": args, "exit": None, "error": type(error).__name__}


def candidate(text):
    match = re.search(r"(?m)^\s*Candidate:\s*(\S+)\s*$", text)
    value = match.group(1) if match else ""
    return value if value != "(none)" and SAFE.fullmatch(value) else None


def transaction(text):
    changes = []
    for line in text.splitlines():
        match = re.match(r"^(Inst|Remv)\s+(\S+)(?:\s+\[([^]]+)\])?(?:\s+\((\S+).*)?$", line)
        if not match and line.startswith(("Inst ", "Remv ")):
            raise ValueError("unrecognized package transaction entry")
        if match:
            action, name, old, new = match.groups()
            if not SAFE.fullmatch(name) or (new and not SAFE.fullmatch(new)):
                raise ValueError("unexpected package transaction entry")
            changes.append({"action": "remove" if action == "Remv" else
                            "upgrade" if old else "install", "package": name,
                            "installed": old, "version": new})
    return changes


def package_sizes(text):
    sizes = {}
    for key, value in re.findall(r"(?m)^(Size|Installed-Size):\s*(\d+)\s*$", text):
        if key in sizes:
            return {}
        sizes[key] = int(value) * (1024 if key == "Installed-Size" else 1)
    return sizes


def index_metadata(root=Path("/var/lib/apt/lists")):
    now = time.time()
    if not root.is_dir():
        return {"available": False}
    files = [p for p in root.iterdir() if p.is_file() and p.name.endswith(("_InRelease", "_Release"))]
    return {"available": True, "release_indexes": [
        {"name": p.name, "age_seconds": max(0, int(now - p.stat().st_mtime))} for p in sorted(files)]}


def linux_inventory():
    policies = {name: run(["apt-cache", "policy", name]) for name in PACKAGES}
    candidates = {name: candidate(result.get("output", "")) for name, result in policies.items()}
    result = {"policies": policies, "candidates": candidates, "indexes": index_metadata(),
              "glibc": run(["ldd", "--version"])}
    result["candidate_details"] = {name: run(["apt-cache", "show", f"{name}={version}"])
                                   for name, version in candidates.items() if version}
    if not all(candidates.values()):
        result["simulation"] = {"blocked": "one or more exact candidates unavailable"}
        return result
    exact = [f"{name}={candidates[name]}" for name in PACKAGES]
    simulation = run(["apt-get", "--simulate", "--no-install-recommends", "install", *exact], 120)
    result["simulation"] = simulation
    if simulation.get("exit") != 0:
        return result
    changes = transaction(simulation["output"])
    result["transaction"] = changes
    for item in changes:
        if item["action"] == "remove" or not item["version"]:
            continue
        spec = f"{item['package']}={item['version']}"
        item["metadata"] = run(["apt-cache", "show", spec])
        item["policy"] = run(["apt-cache", "policy", item["package"]])
        item["sizes"] = package_sizes(item["metadata"].get("output", ""))
    installed = [item for item in changes if item["action"] != "remove"]
    result["download_bytes_sum"] = (sum(i["sizes"]["Size"] for i in installed)
                                    if all("Size" in i.get("sizes", {}) for i in installed) else None)
    result["installed_bytes_sum"] = (sum(i["sizes"]["Installed-Size"] for i in installed)
                                     if all("Installed-Size" in i.get("sizes", {}) for i in installed) else None)
    result["size_note"] = "Sums are package metadata totals, not download cache or installed-size delta."
    return result


def mac_inventory():
    result = {name: run(args) for name, args in {
        "os": ["sw_vers"], "architecture": ["arch"], "developer": ["xcode-select", "-p"],
        "sdk_path": ["xcrun", "--show-sdk-path"], "sdk_version": ["xcrun", "--show-sdk-version"],
        "clt": ["pkgutil", "--pkg-info", "com.apple.pkg.CLTools_Executables"],
        "console_uid": ["stat", "-f", "%u", "/dev/console"]}.items()}
    result["uid"] = os.getuid()
    session_log = io.StringIO()
    try:
        with contextlib.redirect_stdout(session_log):
            verify_existing_session(os.environ.copy())
        result["existing_gui_session"] = "verified"
    except RuntimeError as error:
        result["existing_gui_session"] = str(error)
    result["session_check_log"] = session_log.getvalue()
    return result


def prefix_metadata(name):
    value = os.environ.get(name)
    root = Path(value) if value else None
    if not root or not root.is_absolute() or not root.is_dir():
        return {"path": value, "valid_directory": False}
    files = ("include/mpv/client.h", "lib/cmake/Qt6/Qt6Config.cmake",
             "lib/cmake/MpvQt/MpvQtConfig.cmake", "bin/qmake6", "lib/libmpv.dylib")
    return {"path": str(root), "valid_directory": True,
            "known_files": {rel: (root / rel).is_file() for rel in files}}


def main():
    system = platform.system()
    if system not in ("Linux", "Darwin"):
        raise RuntimeError("native Unix runner required")
    root = Path(__file__).resolve().parents[2]
    prefixes = {name: prefix_metadata(name) for name in
                ("CI_QT_PREFIX", "CI_MPV_PREFIX", "CI_NATIVE_TOOL_PREFIX")}
    report = {"platform": system, "architecture": platform.machine(), "uid": os.getuid(),
              "free_bytes": shutil.disk_usage(root).free, "proposed_owned_prefix": str(root / "deps" / "native-prerequisites"),
              "compiler_and_tool_paths": {name: shutil.which(name) for name in TOOLS},
              "explicit_prefixes": prefixes,
              "details": linux_inventory() if system == "Linux" else mac_inventory()}
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
