"""Validate the actual Qt and split mpv inputs before the desktop build."""
import os
from pathlib import Path
import subprocess

from explicit_mpv import explicit_inputs

MODULES = ("Core", "Gui", "Network", "WebChannel", "Qml", "Quick", "Xml", "WebEngineCore",
           "WebEngineQuick", "Widgets", "OpenGL", "DBus", "Test", "Positioning")


def run(args, *, env, cwd=None, timeout=90):
    result = subprocess.run(list(map(str, args)), cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    print("$ " + " ".join(map(str, args)) + "\n" + result.stdout, flush=True)
    if result.returncode:
        raise RuntimeError("BLOCKED: input probe failed: " + str(args[0]))
    return result.stdout.strip()


def beneath(path, prefix):
    try:
        Path(path).resolve(strict=True).relative_to(prefix)
        return True
    except (OSError, ValueError):
        return False


def check_qt_metadata(qt, env):
    for tool, option in (("qmake6", "-query"), ("qtpaths6", "--query")):
        executable = qt / "bin" / tool
        if not executable.is_file() or not beneath(executable, qt):
            raise RuntimeError("BLOCKED: Qt tool missing: " + tool)
        for key, expected in (("QT_VERSION", "6.9.3"), ("QT_INSTALL_PREFIX", str(qt))):
            value = run([executable, option, key], env=env)
            if (key == "QT_VERSION" and value != expected) or (key != "QT_VERSION" and Path(value).resolve() != qt):
                raise RuntimeError("BLOCKED: relocated Qt " + tool + " " + key + " differs: " + value)
    for name in MODULES:
        module = "Qt6" + name
        if run(["pkg-config", "--modversion", module], env=env) != "6.9.3":
            raise RuntimeError("BLOCKED: unexpected " + module + " version")
        prefix = run(["pkg-config", "--variable=prefix", module], env=env)
        if Path(prefix).resolve() != qt:
            raise RuntimeError("BLOCKED: " + module + " pkg-config prefix differs: " + prefix)
        pcdir = run(["pkg-config", "--variable=pcfiledir", module], env=env)
        if not beneath(pcdir, qt):
            raise RuntimeError("BLOCKED: " + module + " metadata is outside selected Qt")


def check_private_headers(qt):
    for module, header in (("QtGui", "qguiapplication_p.h"), ("QtQuick", "qquickitem_p.h"),
                           ("QtWebEngineCore", "qtwebenginecoreglobal_p.h")):
        path = qt / "include" / module / "6.9.3" / module / "private" / header
        if not path.is_file() or not beneath(path, qt):
            raise RuntimeError("BLOCKED: Qt private header missing: " + str(path))


def check_runtime_files(qt, env):
    paths = [qt / "lib" / ("libQt6" + name + ".so.6") for name in MODULES]
    paths += [qt / "plugins/platforms/libqxcb.so", qt / "libexec/QtWebEngineProcess"]
    for name in ("data", "i18n", "uc"):
        matches = sorted((qt / "lib").glob("libicu" + name + ".so.*"))
        if not matches:
            raise RuntimeError("BLOCKED: shipped ICU library missing: " + name)
        if not beneath(matches[0], qt):
            raise RuntimeError("BLOCKED: shipped ICU escapes selected Qt: " + name)
    for path in paths:
        if not path.is_file() or not beneath(path, qt):
            raise RuntimeError("BLOCKED: missing or nonlocal Qt runtime file " + str(path))
        output = run(["ldd", path], env=env)
        if "not found" in output:
            raise RuntimeError("BLOCKED: unresolved Qt runtime dependency: " + str(path))
        for line in output.splitlines():
            words = line.split()
            if words and words[0].startswith(("libQt6", "libicu")):
                if len(words) < 3 or words[1] != "=>" or not beneath(words[2], qt):
                    raise RuntimeError("BLOCKED: Qt/ICU dependency outside selected prefix: " + line.strip())


def check_executable(build, library, qt, env):
    actual = run([build / "qt_mpv_probe", library], env=env)
    if actual.splitlines()[-2:] != ["6.9.3", str(qt)]:
        raise RuntimeError("BLOCKED: QLibraryInfo version or prefix differs from selected Qt")


def probe_sdk(qt_prefix, job_root, mpv_environment):
    root = Path(job_root).resolve(strict=True)
    qt = Path(qt_prefix).resolve(strict=True)
    if not beneath(qt, root):
        raise RuntimeError("BLOCKED: Qt prefix must be job-owned")
    include, library = explicit_inputs(mpv_environment, "Linux", root)
    env = os.environ.copy()
    env.pop("PKG_CONFIG_LIBDIR", None)
    env["PKG_CONFIG_PATH"] = str(qt / "lib/pkgconfig")
    env["LD_LIBRARY_PATH"] = str(qt / "lib")
    check_qt_metadata(qt, env)
    check_private_headers(qt)
    check_runtime_files(qt, env)
    build = root / "sdk-probe-build"
    build.mkdir(exist_ok=False)
    source = Path(__file__).resolve().parent / "linux_sdk_probe"
    run(["cmake", "-S", source, "-B", build, "-G", "Ninja", "-DQTROOT=" + str(qt),
         "-DMPV_INCLUDE_DIR=" + str(include), "-DMPV_LIBRARY=" + str(library)], env=env)
    run(["cmake", "--build", build, "--parallel", "2"], env=env)
    check_executable(build, library, qt, env)
    return {"qt_prefix": str(qt), "mpv_include": str(include), "mpv_library": str(library),
            "environment": {"PKG_CONFIG_PATH": env["PKG_CONFIG_PATH"],
                            "LD_LIBRARY_PATH": env["LD_LIBRARY_PATH"]}}
