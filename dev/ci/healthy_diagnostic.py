"""Run the isolated, injected healthy-restart diagnostic gate on Unix."""

import os
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET

from check_results import check_junit
from explicit_mpv import cmake_options, default_options, explicit_inputs, verify_cache
from headless_requirements import missing_build_inputs
from native_inventory import incompatible_tools, unavailable_compilers


# These are QCoreApplication/injected tests; no mpv-controller or held fixture.
DIAGNOSTIC_TESTS = (
    "healthy_async_stop_player", "healthy_unpause_player", "healthy_fixture_adapter_cleanup",
    "healthy_healthy_restore_cancel", "healthy_async_stop_source", "healthy_player_native_receipts",
    "healthy_player_restore_cleanup", "healthy_player_restore_observation", "healthy_player_native_restore",
    "healthy_graph_capture", "healthy_graph_capture_lifetime",
)


def require_prerequisites(system, root, environment):
    found = {name: shutil.which(name) for name in ("cmake", "ctest", "ninja", "c++", "git", "pkg-config")}
    missing = [name for name in ("cmake", "ctest", "ninja", "c++", "git") if not found[name]]
    missing += incompatible_tools(found)
    missing += unavailable_compilers(environment)
    explicit = explicit_inputs(environment, system, root)
    missing += missing_build_inputs("static", environment, found["pkg-config"], bool(explicit))
    if missing:
        raise RuntimeError("BLOCKED: diagnostic prerequisites unavailable: " + ", ".join(missing))
    return explicit


def verify_report(path, expected):
    try:
        check_junit(path, expected)
        suite = ET.parse(path).getroot()
        cases = suite.findall("testcase")
        names = [case.get("name") for case in cases]
    except (OSError, ET.ParseError, ValueError) as error:
        raise RuntimeError("Diagnostic CTest report is missing or unsuccessful") from error
    if (suite.tag != "testsuite" or suite.findall(".//testsuite") or
            len(suite.findall(".//testcase")) != len(cases) or suite.get("tests") != str(len(expected)) or
            suite.get("failures") != "0" or
            any(suite.get(field, "0") != "0" for field in ("errors", "disabled", "skipped")) or
            any(case.get("status") != "run" for case in cases) or
            len(names) != len(expected) or set(names) != set(expected)):
        raise RuntimeError("Diagnostic CTest report does not exactly match required tests")


def run(system, root, build, results, command, environment):
    if system not in ("Linux", "Darwin"):
        raise RuntimeError("Unsupported diagnostic platform")
    if not DIAGNOSTIC_TESTS or len(set(DIAGNOSTIC_TESTS)) != len(DIAGNOSTIC_TESTS):
        raise RuntimeError("Diagnostic test selection is empty or duplicated")
    explicit = require_prerequisites(system, root, environment)
    diagnostic = build / "healthy-diagnostic"
    report = results / "healthy-diagnostic-ctest.xml"
    if diagnostic.exists() or diagnostic.is_symlink() or report.exists() or report.is_symlink():
        raise RuntimeError("BLOCKED: diagnostic output already exists")
    options = ["cmake", "-S", str(root), "-B", str(diagnostic), "-G", "Ninja",
               "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-DHEALTHY_RESTART_DIAGNOSTIC_ONLY=ON",
               "-DPLAYER_INTENT_DIAGNOSTIC_ONLY=ON", "-DUSE_STATIC_MPVQT=ON", "-DMPVQT_EXTERNAL_EVENTS=ON"]
    options.extend(cmake_options(explicit) if explicit else default_options(diagnostic / "CMakeCache.txt"))
    prefixes = [environment[key] for key in ("CI_QT_PREFIX", "CI_MPV_PREFIX") if environment.get(key)]
    prefixes += [item for item in environment.get("CMAKE_PREFIX_PATH", "").split(os.pathsep) if item]
    if prefixes:
        options.append("-DCMAKE_PREFIX_PATH=" + ";".join(prefixes))
    command("healthy-diagnostic-configure", options, timeout=300)
    if explicit:
        verify_cache(diagnostic / "CMakeCache.txt", explicit, "static")
    command("healthy-diagnostic-build", ["cmake", "--build", str(diagnostic), "--parallel", "4", "--target",
                                         *DIAGNOSTIC_TESTS], timeout=1800)
    owned = diagnostic / "test-owned"
    owned.mkdir()
    test_env = environment.copy()
    test_env["JELLYFIN_TEST_OWNED_ROOT"] = str(owned)
    expression = "^(" + "|".join(DIAGNOSTIC_TESTS) + ")$"
    command("healthy-diagnostic-ctest", ["ctest", "--test-dir", str(diagnostic), "-R", expression,
            "--no-tests=error", "--timeout", "60", "--output-on-failure", "--output-junit", str(report)],
            env=test_env, timeout=900)
    verify_report(report, DIAGNOSTIC_TESTS)
