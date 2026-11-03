"""Bounded native runner inventory; never installs host packages."""

import os
from pathlib import Path
import platform
import re
import shutil
import subprocess


def probe(args):
    try:
        result = subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
    except (OSError, subprocess.TimeoutExpired) as error:
        return f"unavailable ({type(error).__name__})"
    output = result.stdout.strip().replace("\n", " | ")[:700]
    return f"exit={result.returncode} {output}"


def version_satisfies(text, minimum):
    match = re.search(r"(\d+)\.(\d+)", text)
    return bool(match and (int(match.group(1)), int(match.group(2))) >= minimum)


def incompatible_tools(found):
    incompatible = []
    for name, minimum in (("cmake", (3, 21)), ("ctest", (3, 21)), ("ninja", (1, 10)), ("node", (20, 0))):
        if not found.get(name):
            continue
        try:
            result = subprocess.run([found[name], "--version"], text=True, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=15)
            if result.returncode or not version_satisfies(result.stdout, minimum):
                incompatible.append(name)
        except (OSError, subprocess.TimeoutExpired):
            incompatible.append(name)
    return incompatible


def package_versions():
    if not shutil.which("dpkg-query"):
        print("Display package metadata: dpkg-query unavailable", flush=True)
        return
    for package in ("xvfb", "xauth", "dbus-x11", "mesa-utils"):
        installed = probe(["dpkg-query", "-W", "-f=${Status} ${Version}", package])
        print(f"Package {package}: {installed}", flush=True)
        if shutil.which("apt-cache"):
            try:
                policy = subprocess.run(["apt-cache", "policy", package], text=True, stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL, timeout=15)
                lines = [line.strip() for line in policy.stdout.splitlines()
                         if line.strip().startswith(("Installed:", "Candidate:"))]
                print(f"Package {package} versions: {'; '.join(lines) or 'unavailable'}", flush=True)
            except (OSError, subprocess.TimeoutExpired):
                print(f"Package {package} versions: unavailable", flush=True)


def mpv_files(pkgconfig, allowed):
    for key in ("includedir", "libdir"):
        try:
            result = subprocess.run([pkgconfig, "--variable=" + key, "mpv"], text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=15)
            value = result.stdout.strip()
            path = Path(value).resolve() if value and result.returncode == 0 else None
        except (OSError, subprocess.TimeoutExpired):
            path = None
        if not path or not any(path == base or base in path.parents for base in allowed):
            print(f"mpv {key} file check: unavailable or outside checked prefixes", flush=True)
            continue
        if key == "includedir":
            print(f"mpv header {path / 'mpv/client.h'}: {(path / 'mpv/client.h').is_file()}", flush=True)
        else:
            matches = sorted(str(item) for pattern in ("libmpv.so*", "libmpv*.dylib") for item in path.glob(pattern) if item.is_file())
            print(f"mpv runtime/library candidates: {matches or 'none'}", flush=True)


def inventory(system, mode):
    print(f"Display environment present: {bool(os.environ.get('DISPLAY'))}; session bus present: {bool(os.environ.get('DBUS_SESSION_BUS_ADDRESS'))}", flush=True)
    tools = ["cmake", "ctest", "ninja", "pkg-config", "git", "c++", "node"]
    if system == "Linux":
        tools += ["xvfb-run", "xauth", "dbus-run-session", "glxinfo"]
    else:
        tools += ["xcrun", "launchctl"]
    found = {name: shutil.which(name) for name in tools}
    for name, path in found.items():
        print(f"Tool {name}: {path or 'MISSING'}", flush=True)
    for name in ("cmake", "ctest", "ninja", "node", "c++", "pkg-config"):
        if found.get(name):
            print(f"Version {name}: {probe([found[name], '--version'])}", flush=True)
    if system == "Darwin" and found.get("xcrun"):
        print(f"macOS SDK: {probe([found['xcrun'], '--show-sdk-path'])}", flush=True)
    if system == "Linux":
        release = Path("/etc/os-release")
        if release.is_file():
            for line in release.read_text(encoding="utf-8").splitlines():
                if line.startswith(("ID=", "VERSION_ID=", "PRETTY_NAME=")):
                    print(f"Distribution {line}", flush=True)
        package_versions()
    for key in ("CI_QT_PREFIX", "CI_MPV_PREFIX", "CI_NATIVE_TOOL_PREFIX"):
        value = os.environ.get(key)
        print(f"Explicit {key}: {value or 'not set'}", flush=True)
    modules = ("mpv", "Qt6Core", "Qt6Quick", "Qt6WebEngineQuick", "Qt6WebChannel", "Qt6Positioning")
    if found.get("pkg-config"):
        for module in modules:
            print(f"Module {module}: {probe([found['pkg-config'], '--modversion', module])}", flush=True)
        for key in ("includedir", "libdir"):
            print(f"mpv {key}: {probe([found['pkg-config'], '--variable=' + key, 'mpv'])}", flush=True)
        roots = [Path(root).resolve() for root in ("/usr", "/opt/homebrew", os.environ.get("CI_MPV_PREFIX", "")) if root]
        mpv_files(found["pkg-config"], roots)
    else:
        for module in modules:
            print(f"Module {module}: unavailable (pkg-config missing)", flush=True)
    if shutil.which("qmake6"):
        for key in ("QT_VERSION", "QT_INSTALL_PREFIX"):
            print(f"Qt {key}: {probe(['qmake6', '-query', key])}", flush=True)
    located = set()
    for prefix in filter(None, (os.environ.get("CI_QT_PREFIX"), os.environ.get("CI_MPV_PREFIX"),
                                "/usr", "/usr/local", "/opt/homebrew" if system == "Darwin" else None)):
        root = Path(prefix)
        for rel in ("include/mpv/client.h", "lib/cmake/Qt6/Qt6Config.cmake", "lib/cmake/MpvQt/MpvQtConfig.cmake",
                    "lib/x86_64-linux-gnu/cmake/Qt6/Qt6Config.cmake", "lib/x86_64-linux-gnu/cmake/MpvQt/MpvQtConfig.cmake"):
            candidate = root / rel
            if candidate.is_file():
                print(f"Found prerequisite: {candidate}", flush=True)
                located.add(candidate.name)
    for name in ("client.h", "Qt6Config.cmake", "MpvQtConfig.cmake"):
        if name not in located:
            print(f"Prerequisite {name}: not found in checked prefixes", flush=True)
    print(f"Requested MpvQt mode: {mode}; platform: {platform.system()}/{platform.machine()}", flush=True)
    return found
