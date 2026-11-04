"""Check bounded evidence of build dependencies before optional downloads."""

import os
from pathlib import Path
import shutil
import subprocess


def pkg_exists(program, modules):
    if not program:
        return False
    try:
        return subprocess.run([program, "--exists", *modules], stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL, timeout=15).returncode == 0
    except (OSError, subprocess.SubprocessError):
        return False


def known_prefixes(environment):
    prefixes = [environment.get(key, "") for key in ("CI_QT_PREFIX", "CI_MPV_PREFIX", "QTROOT")]
    prefixes += environment.get("CMAKE_PREFIX_PATH", "").split(os.pathsep)
    prefixes += ["/usr", "/usr/local", "/opt/homebrew"]
    qmake = shutil.which("qmake6")
    if qmake:
        try:
            result = subprocess.run([qmake, "-query", "QT_INSTALL_PREFIX"], text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=15)
            if result.returncode == 0:
                prefixes.append(result.stdout.strip())
        except (OSError, subprocess.SubprocessError):
            pass
    return [Path(value) for value in prefixes if value and Path(value).is_absolute()]


def config_found(prefixes, name):
    return any((prefix / rel / name).is_file() for prefix in prefixes
               for rel in ("lib/cmake/" + name.removesuffix("Config.cmake"),
                           "lib64/cmake/" + name.removesuffix("Config.cmake"),
                           "lib/x86_64-linux-gnu/cmake/" + name.removesuffix("Config.cmake")))


def mpv_found(prefixes):
    for prefix in prefixes:
        if not (prefix / "include/mpv/client.h").is_file():
            continue
        for rel in ("lib", "lib64", "lib/x86_64-linux-gnu"):
            directory = prefix / rel
            if any(path.is_file() for pattern in ("libmpv.so*", "libmpv*.dylib") for path in directory.glob(pattern)):
                return True
    return False


def missing_build_inputs(mode, environment, pkgconfig, explicit_mpv=False):
    prefixes = known_prefixes(environment)
    qt_modules = ("Qt6Core", "Qt6Quick", "Qt6WebEngineQuick", "Qt6WebChannel", "Qt6Positioning")
    missing = []
    if not (pkg_exists(pkgconfig, qt_modules) or config_found(prefixes, "Qt6Config.cmake")):
        missing.append("Qt6 development modules in checked prefixes or pkg-config")
    if not (explicit_mpv or pkg_exists(pkgconfig, ("mpv",)) or mpv_found(prefixes)):
        missing.append("mpv development headers/library in checked prefixes or pkg-config")
    if mode == "system" and not config_found(prefixes, "MpvQtConfig.cmake"):
        missing.append("system MpvQtConfig.cmake in checked prefixes")
    return missing
