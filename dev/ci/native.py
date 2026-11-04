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
from linux_mpris_receipt import run_active_stop, update_obligation
from evidence import update
from explicit_mpv import explicit_inputs, cmake_options, default_options, verify_cache

ROOT = Path(__file__).resolve().parents[2]
BUILD = ROOT / "build"
RESULTS = BUILD / "ci-results"
# Explicitly audited QTEST_APPLESS_MAIN or QTEST_GUILESS_MAIN fixtures.
# Mpv initialization, QTEST_MAIN, QGuiApplication and QQuickWindow stay out.
HEADLESS_TESTS = ("test_systemcomponent", "test_log", "test_settings", "test_displaymanager",
                  "test_log_rotation", "test_chromiumflags", "test_webclient_transport",
                  "test_mpv_test_bootstrap_pure", "test_smtc_admission_model", "test_c2a_hold_gate", "test_c2_error_terminal",
                  "test_c2_fault_quiescence_policy", "test_c2_synthetic_service",
                  "test_c2_fault_restore_permission", "test_c2_fault_restore_frame", "test_c2_capture_observation",
                  "test_c2_quiescence_observation")
LINUX_DESKTOP_TOOLS = frozenset(("xvfb-run", "xauth", "dbus-run-session", "glxinfo"))


def command(name, args, *, env=None, timeout=120):
    held = {"test_renderer_loss_offscreen", "test_mpris_active_stop", "test_c2_retirement_native"}
    if any(Path(str(arg)).name.removesuffix(".exe") in held for arg in args):
        raise RuntimeError("BLOCKED: native render fixture violates the render-thread contract")
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


def preflight(prepared_tools=False):
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
    explicit = explicit_inputs(os.environ, system, ROOT)
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
    if prepared_tools and bootstrap_missing:
        raise RuntimeError("BLOCKED: prepared native tools are unavailable: " + ", ".join(bootstrap_missing))
    if prepared_tools:
        absent = missing_build_inputs(mode, os.environ, found.get("pkg-config"), bool(explicit))
        if absent:
            raise RuntimeError("BLOCKED: prepared build inputs not found: " + ", ".join(absent))
    if bootstrap_missing:
        absent = missing_build_inputs(mode, os.environ, found.get("pkg-config"), bool(explicit))
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


def fresh_sdk_workspace(root, job):
    if not job.isdecimal():
        raise RuntimeError("BLOCKED: numeric CI_JOB_ID required for job-local SDK")
    root = root.resolve(strict=True)
    if root.stat().st_uid != os.getuid():
        raise RuntimeError("BLOCKED: checkout is not owned by the CI job user")
    deps = root / "deps"
    if deps.is_symlink():
        raise RuntimeError("BLOCKED: job dependency directory is a symlink")
    deps.mkdir(exist_ok=True)
    if not deps.is_dir() or deps.stat().st_uid != os.getuid() or deps.resolve().parent != root:
        raise RuntimeError("BLOCKED: job dependency directory is not owned by this checkout")
    workspace = deps / ("ci-" + job)
    try:
        workspace.mkdir(mode=0o700)
    except FileExistsError as error:
        raise RuntimeError("BLOCKED: job SDK workspace already exists") from error
    return workspace


def prepare_linux_sdk():
    if (platform.system(), platform.machine().lower()) != ("Linux", "x86_64") or os.environ.get("CI_JOB_IMAGE"):
        raise RuntimeError("BLOCKED: job-local SDK requires native Linux x86_64")
    if os.environ.get("CI_MPVQT_MODE") != "static":
        raise RuntimeError("BLOCKED: job-local SDK currently supports static mode only")
    conflicts = ("CI_QT_PREFIX", "CI_MPV_PREFIX", "CI_MPV_DEV_DEB", "CI_MPV_INCLUDE_DIR", "CI_MPV_LIBRARY", "CI_MPV_PINNED_RUNTIME")
    if any(os.environ.get(key) for key in conflicts):
        raise RuntimeError("BLOCKED: job-local SDK conflicts with existing explicit inputs")
    if shutil.disk_usage(ROOT).free < 10 * 1024**3:
        raise RuntimeError("BLOCKED: at least 10 GiB free space required")
    if unavailable_compilers(os.environ) or not all(shutil.which(name) for name in ("cc", "c++", "pkg-config", "git")):
        raise RuntimeError("BLOCKED: native compiler and host tools required")
    try:
        from linux_qt_archive import stage_qt, complete_qt
        from linux_mpv_payload import acquire_mpv
        from linux_sdk_probe import probe_sdk
    except ImportError as error:
        raise RuntimeError("BLOCKED: validated job-local SDK helpers are unavailable") from error
    found = inventory("Linux", "static")
    missing = {name for name in ("cmake", "ctest", "ninja", "node") if not found.get(name)}
    missing.update(name for name in incompatible_tools(found) if name in ("cmake", "ctest", "ninja", "node"))
    if "ctest" in missing:
        missing.add("cmake")
    workspace = fresh_sdk_workspace(ROOT, os.environ.get("CI_JOB_ID", ""))
    if missing:
        bins = bootstrap(missing, workspace / "native-tools")
        os.environ["PATH"] = os.pathsep.join([str(path) for path in bins] + [os.environ.get("PATH", "")])
    candidate = stage_qt(workspace)
    qt_prefix = Path(candidate.prefix).resolve(strict=True)
    try:
        qt_prefix.relative_to(workspace.resolve(strict=True))
    except ValueError as error:
        raise RuntimeError("BLOCKED: staged Qt prefix is outside the job workspace") from error
    mpv_paths = acquire_mpv(workspace)
    expected = {"CI_MPV_DEV_DEB", "CI_MPV_INCLUDE_DIR", "CI_MPV_LIBRARY", "CI_MPV_PINNED_RUNTIME"}
    if set(mpv_paths) != expected or mpv_paths["CI_MPV_PINNED_RUNTIME"] != "1":
        raise RuntimeError("BLOCKED: incomplete staged mpv identity")
    explicit_inputs({**os.environ, **mpv_paths}, "Linux", ROOT)
    report = probe_sdk(qt_prefix, workspace, mpv_paths)
    updates = report.get("environment", {})
    if set(updates) != {"PKG_CONFIG_PATH", "LD_LIBRARY_PATH"} or not all(isinstance(v, str) and v for v in updates.values()):
        raise RuntimeError("BLOCKED: SDK probe did not provide validated runtime paths")
    if (Path(updates["PKG_CONFIG_PATH"]).resolve() != qt_prefix / "lib/pkgconfig" or
            Path(updates["LD_LIBRARY_PATH"]).resolve() != qt_prefix / "lib"):
        raise RuntimeError("BLOCKED: SDK probe returned paths outside the selected Qt prefix")
    complete_qt(candidate, report)
    if Path(candidate.prefix).resolve(strict=True) != qt_prefix:
        raise RuntimeError("BLOCKED: Qt prefix changed after the SDK probe")
    os.environ.update({"CI_QT_PREFIX": str(qt_prefix), **mpv_paths, **updates})


def portable_checks():
    command("fixture-suspend-policy-tests", [sys.executable, "dev/ci/test_fixture_suspend_policy.py"])
    command("qga-terminal-tests", [sys.executable, "dev/ci/test_qga_terminal.py"])
    command("mac-sdist-static-tests", [sys.executable, "dev/ci/test_macos_sdist_static.py"])
    command("mac-sdist-supervisor-tests", [sys.executable, "dev/ci/test_macos_sdist_supervise.py"])
    command("linux-qt-cache-tests", [sys.executable, "dev/ci/test_linux_qt_cache.py"])
    command("mac-carchive-tests", [sys.executable, "dev/ci/test_mac_carchive.py"])
    command("mac-helper-tests", [sys.executable, "dev/ci/test_mac_helpers.py"])
    command("mac-helper-acquisition-tests", [sys.executable, "dev/ci/test_mac_helpers_acquire.py"])
    command("report-gate-tests", [sys.executable, "tests/test_ci_results.py"])
    command("evidence-tests", [sys.executable, "dev/ci/test_evidence.py"])
    command("linux-mpris-receipt-tests", [sys.executable, "dev/ci/test_linux_mpris_receipt.py"])
    command("build-manifest-tests", [sys.executable, "dev/ci/test_build_manifest.py"])
    command("acceptance-tests", [sys.executable, "dev/ci/test_acceptance.py"])
    command("archive-safety-tests", [sys.executable, "dev/ci/test_safe_extract.py"])
    command("inventory-tests", [sys.executable, "dev/ci/test_native_inventory.py"])
    command("native-prerequisite-inventory-tests", [sys.executable, "dev/ci/test_native_prereq_inventory.py"])
    command("explicit-mpv-tests", [sys.executable, "dev/ci/test_explicit_mpv.py"])
    command("native-order-tests", [sys.executable, "dev/ci/test_native_order.py"])
    command("linux-sdk-order-tests", [sys.executable, "dev/ci/test_linux_sdk_order.py"])
    command("linux-sdk-input-tests", [sys.executable, "dev/ci/test_linux_sdk_inputs.py"])
    command("qt-sdk-archive-tests", [sys.executable, "dev/ci/test_qt_sdk_archive.py"])
    command("linux-qt-plan-tests", [sys.executable, "dev/ci/test_linux_qt_plan.py"])
    command("linux-qt-pc-tests", [sys.executable, "dev/ci/test_linux_qt_pc.py"])
    command("libarchive-reader-contract-tests", [sys.executable, "dev/ci/test_libarchive_reader_contract.py"])
    command("qt-unix-manifest-tests", [sys.executable, "dev/ci/test_qt_unix_manifest.py"])
    command("mac-session-tests", [sys.executable, "dev/ci/test_macos_session.py"])
    command("headless-routing-tests", [sys.executable, "dev/ci/test_headless_routing.py"])


def verify_mpvqt_source():
    command("mpvqt-source", ["git", "-C", "external/mpvqt", "rev-parse", "HEAD"])
    sha = subprocess.check_output(["git", "-C", "external/mpvqt", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    if sha != "2ebbd38bf65d01be9c55d673ecd43c8bb9a6fc8e":
        raise RuntimeError("Unexpected mpvqt source revision")


def configure(system, mode):
    command("compiler-version", ["c++", "--version"])
    command("cmake-version", ["cmake", "--version"])
    explicit = explicit_inputs(os.environ, system, ROOT)
    if explicit:
        include, library = explicit
        probe = BUILD / "mpv-link-probe"
        command("mpv-probe-build", ["cc", str(ROOT / "dev/ci/mpv_link_probe.c"), "-I", str(include),
                str(library), "-ldl", "-Wl,-rpath," + str(library.parent), "-o", str(probe)])
        command("mpv-probe", [str(probe), str(library)])
    else:
        command("mpv-version", ["pkg-config", "--modversion", "mpv"])
    if system == "Linux":
        command("qt-versions", ["pkg-config", "--modversion", "Qt6Core", "Qt6WebEngineQuick", "Qt6WebChannel", "Qt6Positioning"])
    elif shutil.which("qmake6"):
        command("qt-version", ["qmake6", "-query", "QT_VERSION"])
        command("qt-prefix", ["qmake6", "-query", "QT_INSTALL_PREFIX"])
    else:
        print("qmake6 unavailable; CMake configure records Qt version and modules", flush=True)
    verify_mpvqt_source()
    options = ["cmake", "-S", str(ROOT), "-B", str(BUILD), "-G", "Ninja",
               "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DUSE_STATIC_MPVQT=" + ("ON" if mode == "static" else "OFF"),
               "-DMPVQT_EXTERNAL_EVENTS=" + ("ON" if mode == "static" else "OFF")]
    if explicit:
        options.extend(cmake_options(explicit))
    else:
        options.extend(default_options(BUILD / "CMakeCache.txt"))
    prefixes = [os.environ[key] for key in ("CI_QT_PREFIX", "CI_MPV_PREFIX") if os.environ.get(key)]
    prefixes += [prefix for prefix in os.environ.get("CMAKE_PREFIX_PATH", "").split(os.pathsep) if prefix]
    if prefixes:
        options.append("-DCMAKE_PREFIX_PATH=" + ";".join(prefixes))
    command("configure", options, timeout=300)
    if explicit:
        verify_cache(BUILD / "CMakeCache.txt", explicit, mode)
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
    raise RuntimeError("BLOCKED: offscreen render fixture violates the render-thread contract")


def run_scenarios(fixture, prefix, env, media):
    if fixture == "test_renderer_loss_offscreen":
        raise RuntimeError("BLOCKED: offscreen render fixture violates the render-thread contract")
    for scenario in ("playing", "paused", "held-hook"):
        evidence = RESULTS / fixture / scenario
        evidence.mkdir(parents=True, exist_ok=fixture != "test_renderer_loss_offscreen")
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


def run_stages(system, mode, desktop_missing):
    configure(system, mode)
    from build_manifest import capture
    capture(ROOT, Path(os.environ["CI_EVIDENCE_DIR"]), BUILD / "tests/test_renderer_loss_offscreen")
    headless_checks()
    directory = Path(os.environ["CI_EVIDENCE_DIR"])
    update(directory, "build_core", "passed", "Native build and classified core checks passed")
    update(directory, "offscreen", "blocked", "Native render-thread contract is unresolved; fixture held before launch")
    try:
        desktop_preflight(system, desktop_missing)
    except RuntimeError as error:
        if system == "Linux":
            update_obligation(directory, "blocked", str(error))
        if system == "Darwin":
            print(f"BLOCKED: macOS offscreen runtime: {error}", flush=True)
            return
        raise
    runtime_ctest(system, mode)
    if system == "Linux":
        update_obligation(directory, "blocked", "Native render-thread contract is unresolved; fixture held before launch")


def main():
    RESULTS.mkdir(parents=True, exist_ok=True)
    portable_checks()
    command("native-prereq-inventory", [sys.executable, "dev/ci/native_prereq_inventory.py"], timeout=600)
    sdk_mode = os.environ.get("CI_LINUX_SDK_MODE", "")
    if sdk_mode not in ("", "job-local"):
        raise RuntimeError("BLOCKED: unsupported CI_LINUX_SDK_MODE")
    if sdk_mode:
        prepare_linux_sdk()
        system, mode, desktop_missing = preflight(prepared_tools=True)
    else:
        system, mode, desktop_missing = preflight()
    verify_mpvqt_source()
    patch_env = os.environ.copy()
    patch_env["MPVQT_TEST_SOURCE"] = str(ROOT / "external/mpvqt")
    command("shared-patch-tests", [sys.executable, "tests/test_mpvqt_shared_patch.py"], env=patch_env)
    command("c2-patch-tests", [sys.executable, "tests/test_mpvqt_c2_patch.py"], env=patch_env)
    command("windows-smoke-result-tests", ["node", "--test", "tests/test_windows_smoke_result.mjs"])
    command("explicit-mpv-cmake-tests", [sys.executable, "dev/ci/test_explicit_mpv_cmake.py"])
    command("linux-sdk-cmake-tests", [sys.executable, "dev/ci/test_linux_sdk_cmake.py"])
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
