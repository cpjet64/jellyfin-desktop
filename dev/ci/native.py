"""Native Unix CI gate; missing runner capabilities are hard failures."""

import os
from pathlib import Path
import platform
import shutil
import signal
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from native_bootstrap import bootstrap
from native_inventory import incompatible_tools, inventory, prepare_process_paths, unavailable_compilers
from macos_session import verify_existing_session
from headless_requirements import missing_build_inputs
from evidence import update

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build"
RESULTS = BUILD / "ci-results"
# Explicitly audited QTEST_APPLESS_MAIN or QTEST_GUILESS_MAIN fixtures.
# Mpv initialization, QTEST_MAIN, QGuiApplication and QQuickWindow stay out.
HEADLESS_TESTS = ("test_systemcomponent", "test_log", "test_settings", "test_displaymanager",
                  "test_log_rotation", "test_chromiumflags", "test_webclient_transport",
                  "test_mpv_test_bootstrap_pure")
LINUX_DESKTOP_TOOLS = frozenset(("xvfb-run", "xauth", "dbus-run-session", "glxinfo"))


def command(name, args, *, env=None, timeout=120):
    print(f"Running {name}", flush=True)
    log = RESULTS / f"{name}.log"
    with log.open("w", encoding="utf-8") as output:
        process = subprocess.Popen(args, cwd=ROOT, env=env, stdout=output,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            raise RuntimeError(f"{name} exceeded {timeout}s; see {log}")
        finally:
            try:
                os.killpg(process.pid, 0)
            except ProcessLookupError:
                pass
            else:
                os.killpg(process.pid, signal.SIGKILL)
                raise RuntimeError(f"{name} left descendant processes; see {log}")
    if code:
        raise RuntimeError(f"{name} failed ({code}); see {log}")


def preflight():
    system = platform.system()
    machine = platform.machine().lower()
    if system == "Darwin":
        os.environ.pop("CI_DEDICATED_GUI_SESSION", None)
    free = shutil.disk_usage(ROOT).free
    print(f"Runner OS={system} architecture={machine} free_bytes={free}", flush=True)
    print(f"CI job={os.environ.get('CI_JOB_ID', 'unknown')} runner={os.environ.get('CI_RUNNER_ID', 'unknown')} runner_arch={os.environ.get('CI_RUNNER_EXECUTABLE_ARCH', 'unknown')}", flush=True)
    mode = os.environ.get("CI_MPVQT_MODE")
    prefixes = [os.environ[key] for key in ("CI_QT_PREFIX", "CI_MPV_PREFIX", "CI_NATIVE_TOOL_PREFIX") if os.environ.get(key)]
    invalid_prefixes = [prefix for prefix in prefixes if not Path(prefix).is_absolute() or not Path(prefix).is_dir()]
    valid_prefixes = [prefix for prefix in prefixes if prefix not in invalid_prefixes]
    standard_bins = ["/usr/local/bin", "/usr/bin", "/bin"]
    if system == "Darwin":
        standard_bins = ["/opt/homebrew/bin"] + standard_bins + ["/usr/sbin", "/sbin"]
    prepare_process_paths(os.environ, valid_prefixes, standard_bins)
    found = inventory(system, mode)
    unavailable = unavailable_compilers(os.environ)
    if (system, machine) not in (("Linux", "x86_64"), ("Darwin", "x86_64")):
        raise RuntimeError(f"Unsupported native runner: {system}/{machine}")
    if os.environ.get("CI_JOB_IMAGE"):
        raise RuntimeError("BLOCKED: native shell runner required")
    if free < 10 * 1024**3:
        raise RuntimeError("BLOCKED: at least 10 GiB free space required")
    if invalid_prefixes:
        raise RuntimeError("BLOCKED: explicit native prefixes must be existing absolute directories")
    if unavailable:
        raise RuntimeError("BLOCKED: configured compiler unavailable: " + ", ".join(unavailable))
    missing = [tool for tool, path in found.items() if not path]
    for tool in incompatible_tools(found):
        print(f"Tool {tool}: incompatible version", flush=True)
        if tool not in missing:
            missing.append(tool)
    can_bootstrap = {"cmake", "ctest", "ninja", "node"} if system == "Linux" else set()
    desktop_missing = [name for name in missing if system == "Linux" and name in LINUX_DESKTOP_TOOLS]
    host_missing = [name for name in missing if name not in can_bootstrap and name not in desktop_missing]
    if host_missing:
        raise RuntimeError("BLOCKED: missing host prerequisites: " + ", ".join(host_missing))
    bootstrap_missing = [name for name in missing if name in can_bootstrap]
    if bootstrap_missing:
        absent = missing_build_inputs(mode, os.environ, found.get("pkg-config"))
        if absent:
            raise RuntimeError("BLOCKED: build inputs not found before tool bootstrap: " + ", ".join(absent))
        job = os.environ.get("CI_JOB_ID", "")
        if not job.isdecimal():
            raise RuntimeError("BLOCKED: numeric CI_JOB_ID required for job-local tools")
        requested = set(bootstrap_missing)
        if "ctest" in requested:
            requested.add("cmake")
        bins = bootstrap(requested, ROOT / "deps" / ("ci-" + job) / "native-tools")
        os.environ["PATH"] = os.pathsep.join([str(path) for path in bins] + [os.environ.get("PATH", "")])
        for name in bootstrap_missing:
            if not shutil.which(name):
                raise RuntimeError(f"BLOCKED: bootstrapped tool unavailable: {name}")
    if mode not in ("static", "system"):
        raise RuntimeError("CI_MPVQT_MODE must be static or system")
    return system, mode, desktop_missing


def portable_checks():
    command("report-gate-tests", [sys.executable, "tests/test_ci_results.py"])
    command("evidence-tests", [sys.executable, "dev/ci/test_evidence.py"])
    command("acceptance-tests", [sys.executable, "dev/ci/test_acceptance.py"])
    command("archive-safety-tests", [sys.executable, "dev/ci/test_safe_extract.py"])
    command("inventory-tests", [sys.executable, "dev/ci/test_native_inventory.py"])
    command("mac-session-tests", [sys.executable, "dev/ci/test_macos_session.py"])
    command("headless-routing-tests", [sys.executable, "dev/ci/test_headless_routing.py"])
    command("shared-patch-tests", [sys.executable, "tests/test_mpvqt_shared_patch.py"])


def configure(system, mode):
    command("compiler-version", ["c++", "--version"])
    command("cmake-version", ["cmake", "--version"])
    command("mpv-version", ["pkg-config", "--modversion", "mpv"])
    if system == "Linux":
        command("qt-versions", ["pkg-config", "--modversion", "Qt6Core", "Qt6WebEngineQuick", "Qt6WebChannel", "Qt6Positioning"])
    elif shutil.which("qmake6"):
        command("qt-version", ["qmake6", "-query", "QT_VERSION"])
        command("qt-prefix", ["qmake6", "-query", "QT_INSTALL_PREFIX"])
    else:
        print("qmake6 unavailable; CMake configure records Qt version and modules", flush=True)
    command("mpvqt-source", ["git", "-C", "external/mpvqt", "rev-parse", "HEAD"])
    sha = subprocess.check_output(["git", "-C", "external/mpvqt", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    if sha != "2ebbd38bf65d01be9c55d673ecd43c8bb9a6fc8e":
        raise RuntimeError("Unexpected mpvqt source revision")
    options = ["cmake", "-S", str(ROOT), "-B", str(BUILD), "-G", "Ninja",
               "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DUSE_STATIC_MPVQT=" + ("ON" if mode == "static" else "OFF"),
               "-DMPVQT_EXTERNAL_EVENTS=" + ("ON" if mode == "static" else "OFF")]
    prefixes = [os.environ[key] for key in ("CI_QT_PREFIX", "CI_MPV_PREFIX") if os.environ.get(key)]
    prefixes += [prefix for prefix in os.environ.get("CMAKE_PREFIX_PATH", "").split(os.pathsep) if prefix]
    if prefixes:
        options.append("-DCMAKE_PREFIX_PATH=" + ";".join(prefixes))
    command("configure", options, timeout=300)
    command("build", ["cmake", "--build", str(BUILD), "--parallel", "4"], timeout=1800)


def headless_checks():
    expression = "^(" + "|".join(HEADLESS_TESTS) + ")$"
    report_path = RESULTS / "headless-ctest.xml"
    command("headless-ctest", ["ctest", "--test-dir", str(BUILD), "-R", expression,
            "--output-on-failure", "--no-tests=error", "--timeout", "60", "--output-junit", str(report_path)], timeout=900)
    report = [sys.executable, "dev/ci/check_results.py", "junit", str(report_path)]
    for name in HEADLESS_TESTS:
        report += ["--require", name]
    command("headless-ctest-report", report)
    command("input-plugin", ["node", "--test", "tests/test_inputPlugin.js"], timeout=120)
    command("bundle-paths", [sys.executable, "tests/test_bundle_paths.py"], timeout=120)
    command("package-freshness", [sys.executable, "tests/test_windows_package_freshness.py"], timeout=300)
    print(f"PASS: {len(HEADLESS_TESTS)} explicitly classified headless CTest cases", flush=True)


def desktop_preflight(system, desktop_missing):
    if desktop_missing:
        raise RuntimeError("BLOCKED: missing native desktop prerequisites: " + ", ".join(desktop_missing))
    if system == "Darwin":
        # MpvQt uses Qt ConfigLocation (~/Library/Preferences on macOS).
        # Inspect existence only; never read the session owner's configuration.
        config = Path.home() / "Library/Preferences/mpvqt/mpvqt.conf"
        print(f"MpvQt writable configuration candidate: {config}", flush=True)
        try:
            config.lstat()
        except FileNotFoundError:
            pass
        else:
            raise RuntimeError("BLOCKED: existing non-job-owned mpvqt configuration would be consumed")
        verify_existing_session(os.environ)


def runtime_ctest(system, mode):
    prefix = ["xvfb-run", "-a", "-s", "-screen 0 1280x800x24 +extension GLX",
              "dbus-run-session", "--"] if system == "Linux" else []
    command("ctest", prefix + ["ctest", "--test-dir", str(BUILD), "-LE", "gui", "--output-on-failure",
                               "--no-tests=error", "--timeout", "60", "--output-junit", str(RESULTS / "runtime-ctest.xml")], timeout=900)
    report = [sys.executable, "dev/ci/check_results.py", "junit", str(RESULTS / "runtime-ctest.xml")]
    required = ["test_fault_stop", "test_webclient_transport", "test_webclient_fault_guard", "test_mpv_test_bootstrap"]
    if mode == "static":
        required.extend(["test_mpvqt_event_ownership", "test_mpvqt_player_binding", "test_player_released_state"])
    if system == "Linux":
        required.append("test_mpris_fault")
    for name in required:
        report += ["--require", name]
    command("ctest-report", report)


def gui(system):
    env = os.environ.copy()
    env["QSG_INFO"] = "1"
    env["QT_QPA_PLATFORM"] = "xcb" if system == "Linux" else "cocoa"
    media = RESULTS / "recovery.y4m"
    command("media", [sys.executable, "tests/generate_recovery_media.py", str(media)], timeout=120)
    prefix = ["xvfb-run", "-a", "-s", "-screen 0 1280x800x24 +extension GLX", "dbus-run-session", "--"] if system == "Linux" else []
    if system == "Linux":
        command("render-backend", prefix + ["glxinfo", "-B"], env=env)
    executable = BUILD / "tests" / "test_webclient_fault_webengine"
    command("webengine", prefix + [str(executable), "-o", str(RESULTS / "webengine.txt") + ",txt"], env=env, timeout=90)
    if not (RESULTS / "webengine.txt").is_file():
        raise RuntimeError("WebEngine test did not produce its result")
    command("webengine-report", [sys.executable, "dev/ci/check_results.py", "qtest", str(RESULTS / "webengine.txt")])
    for fixture in ("test_renderer_loss_playback", "test_webclient_containment"):
        run_scenarios(fixture, prefix, env, media)


def offscreen(system):
    env = os.environ.copy()
    env.update({"QSG_INFO": "1", "QT_QPA_PLATFORM": "xcb" if system == "Linux" else "cocoa"})
    prefix = ["xvfb-run", "-a", "-s", "-screen 0 1280x800x24 +extension GLX",
              "dbus-run-session", "--"] if system == "Linux" else []
    media = RESULTS / "recovery.y4m"
    command("offscreen-media", [sys.executable, "tests/generate_recovery_media.py", str(media)])
    run_scenarios("test_renderer_loss_offscreen", prefix, env, media)


def run_scenarios(fixture, prefix, env, media):
    for scenario in ("playing", "paused", "held-hook"):
        evidence = RESULTS / fixture / scenario
        evidence.mkdir(parents=True, exist_ok=True)
        local = env.copy()
        local.update({"RECOVERY_SCENARIO": scenario, "RECOVERY_MEDIA": str(media), "RECOVERY_EVIDENCE_DIR": str(evidence)})
        command(f"{fixture}-{scenario}", prefix + [str(BUILD / "tests" / fixture),
                "-o", str(evidence / "qtest.txt") + ",txt"], env=local, timeout=90)
        if not (evidence / "qtest.txt").is_file():
            raise RuntimeError(f"Missing {fixture} {scenario} result")
        command(f"{fixture}-{scenario}-report", [sys.executable, "dev/ci/check_results.py", "qtest", str(evidence / "qtest.txt")])
        if fixture == "test_renderer_loss_playback" and scenario != "held-hook":
            if any(not (evidence / name).is_file() for name in ("before.png", "after.png")):
                raise RuntimeError(f"Missing {scenario} image evidence")
        if fixture == "test_webclient_containment" and scenario != "held-hook":
            if any(not (evidence / name).is_file() for name in ("containment-before.png", "containment-after.png")):
                raise RuntimeError(f"Missing containment {scenario} image evidence")
        if fixture == "test_renderer_loss_offscreen" and scenario != "held-hook":
            images = ["offscreen-before.png", "offscreen-after.png"]
            if scenario == "playing":
                images.append("offscreen-after2.png")
            if any(not (evidence / name).is_file() for name in images):
                raise RuntimeError(f"Missing offscreen {scenario} image evidence")


def run_stages(system, mode, desktop_missing):
    configure(system, mode)
    headless_checks()
    directory = Path(os.environ["CI_EVIDENCE_DIR"])
    update(directory, "build_core", "passed", "Native build and classified core checks passed")
    update(directory, "offscreen", "not_run", "Checking native runtime prerequisites")
    try:
        desktop_preflight(system, desktop_missing)
    except RuntimeError as error:
        update(directory, "offscreen", "blocked", str(error))
        if system == "Darwin":
            print(f"BLOCKED: macOS offscreen runtime: {error}", flush=True)
            return
        raise
    runtime_ctest(system, mode)
    offscreen(system)
    update(directory, "offscreen", "passed", "All three native OpenGL scenarios passed")


def main():
    RESULTS.mkdir(parents=True, exist_ok=True)
    portable_checks()
    system, mode, desktop_missing = preflight()
    with tempfile.TemporaryDirectory(prefix="ci-runtime-", dir=BUILD) as runtime:
        os.environ["JELLYFIN_TEST_OWNED_ROOT"] = str(BUILD)
        for name in ("XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_RUNTIME_DIR"):
            path = Path(runtime) / name.lower()
            path.mkdir(mode=0o700)
            os.environ[name] = str(path)
        run_stages(system, mode, desktop_missing)
    print(f"Completed {system} {mode} headless job; desktop acceptance remains blocked")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        if str(error).startswith("BLOCKED:") and os.environ.get("CI_EVIDENCE_DIR"):
            path = Path(os.environ["CI_EVIDENCE_DIR"]) / "evidence.json"
            import json
            report = json.loads(path.read_text(encoding="utf-8"))
            if report.get("active_phase"):
                update(path.parent, report["active_phase"], "blocked", str(error))
        print(f"CI failure: {error}", file=sys.stderr)
        sys.exit(1)
