"""Check that the mandatory CMake test cannot precede tool preparation."""
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import native


class NativeOrderTest(unittest.TestCase):
    def test_headless_player_gate_is_required(self):
        calls = []
        def run(name, args, **_kwargs):
            calls.append((name, args))
            if name == 'mpv-video-player':
                raise RuntimeError('player test failure')
        with patch.object(native, 'command', side_effect=run):
            with self.assertRaisesRegex(RuntimeError, 'player test failure'):
                native.headless_checks()
        self.assertEqual(calls[-1], ('mpv-video-player', ['node', '--test', 'tests/test_mpvVideoPlayer.js',
                                                          'tests/test_mpvVideoPlayer_order.js', 'tests/test_synchronous_stop_page.js',
                                                          'tests/test_playback_intent_acceptance.js', 'tests/test_recovery_plugin_acceptance.js']))

    def test_producer_gate_runs_after_player_and_propagates_failure(self):
        calls = []
        child_env = []
        def run(name, args, **kwargs):
            calls.append((name, args))
            if name == 'jellyfin-recovery-producer':
                child_env.append(kwargs['env'])
                raise RuntimeError('producer test failure')
        previous = os.environ.get('JELLYFIN_WEB_SOURCE')
        with patch.object(native, 'command', side_effect=run):
            with self.assertRaisesRegex(RuntimeError, 'producer test failure'):
                native.headless_checks()
        self.assertEqual(calls[-3][0], 'mpv-video-player')
        self.assertEqual(calls[-2], ('jellyfin-source-provision',
                                     [native.sys.executable, 'dev/ci/provision_jellyfin_web.py']))
        self.assertEqual(calls[-1], ('jellyfin-recovery-producer', ['node', '--test',
                         'tests/test_jellyfin_recovery_transaction.mjs', 'tests/test_jellyfin_recovery_manager.cjs',
                         'tests/test_jellyfin_recovery_transition.cjs']))
        self.assertEqual(child_env[0]['JELLYFIN_WEB_SOURCE'], str(native.ROOT / 'deps/ci-jellyfin-web/playbackmanager.js'))
        self.assertEqual(os.environ.get('JELLYFIN_WEB_SOURCE'), previous)

    def test_failed_provision_stops_before_producer_tests(self):
        calls = []
        def run(name, _args, **_kwargs):
            calls.append(name)
            if name == 'jellyfin-source-provision':
                raise RuntimeError('invalid source')
        with patch.object(native, 'command', side_effect=run):
            with self.assertRaisesRegex(RuntimeError, 'invalid source'):
                native.headless_checks()
        self.assertEqual(calls[-1], 'jellyfin-source-provision')
        self.assertNotIn('jellyfin-recovery-producer', calls)

    def exercise(self, available, blocked=False, system="Linux", mode="static", node_available=True,
                 smoke_failed=False, c2_failed=False, source_failed=False, healthy_failed=False):
        events = []
        found = {key: "/tool" for key in ("cmake", "ctest", "ninja", "node", "pkg-config", "git", "c++")}
        found["cmake"] = "/tool" if available else None
        found["node"] = "/tool" if node_available else None
        def command(name, args, **kwargs):
            events.append(name)
            if name == "windows-smoke-result-tests" and smoke_failed:
                raise RuntimeError("smoke regression failed")
            if name == "c2-patch-tests" and c2_failed:
                raise RuntimeError("c2 regression failed")
            if name == "healthy-copy-tests":
                self.assertEqual(args, [native.sys.executable, "tests/test_healthy_restart_acceptance.py"])
                env = kwargs["env"]
                self.assertEqual(env["MPVQT_TEST_SOURCE"], str(native.ROOT / "external/mpvqt"))
                self.assertEqual(env["HEALTHY_TEST_SOURCE"], str(native.ROOT))
                self.assertEqual(env["CMAKE_GENERATOR"], "Ninja")
                self.assertEqual(env["TMPDIR"], str(native.RESULTS))
                self.assertEqual(env["TEMP"], str(native.RESULTS))
                self.assertEqual(env["TMP"], str(native.RESULTS))
                self.assertNotIn("CMAKE_GENERATOR_PLATFORM", env)
                self.assertNotIn("CMAKE_GENERATOR_TOOLSET", env)
                self.assertNotIn("CMAKE_GENERATOR_INSTANCE", env)
                self.assertEqual(os.environ["CMAKE_GENERATOR_PLATFORM"], "conflict")
                self.assertEqual(os.environ["HEALTHY_TEST_SOURCE"], "hostile")
                if healthy_failed:
                    raise RuntimeError("healthy copy failed")
        def verify_source():
            events.append("verify-mpvqt-source")
            if source_failed:
                raise RuntimeError("missing mpvqt source")
        def bootstrap(requested, destination):
            events.append("bootstrap")
            expected = ({"cmake"} if not available else set()) | ({"node"} if not node_available else set())
            self.assertEqual(requested, expected)
            if blocked:
                raise RuntimeError("BLOCKED: bootstrap failed")
            return [Path("/job/bin")]
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp, \
             patch.object(native, "BUILD", Path(tmp)), patch.object(native, "RESULTS", Path(tmp) / "results"), \
             patch.dict(os.environ, {"CI_JOB_ID": "42", "CI_MPVQT_MODE": mode,
                                    "CMAKE_GENERATOR_PLATFORM": "conflict", "HEALTHY_TEST_SOURCE": "hostile"}, clear=True), \
             patch.object(native.platform, "system", return_value=system), \
             patch.object(native.platform, "machine", return_value="x86_64"), \
             patch.object(native.shutil, "disk_usage", return_value=SimpleNamespace(free=20 * 1024**3)), \
             patch.object(native, "inventory", return_value=found), \
             patch.object(native, "unavailable_compilers", return_value=[]), \
             patch.object(native, "incompatible_tools", return_value=[]), \
             patch.object(native, "explicit_inputs", return_value=None), \
             patch.object(native, "missing_build_inputs", return_value=[]), \
             patch.object(native, "bootstrap", side_effect=bootstrap), \
             patch.object(native.shutil, "which", return_value="/job/bin/cmake"), \
             patch.object(native, "portable_checks", side_effect=lambda: events.append("portable")), \
             patch.object(native, "verify_mpvqt_source", side_effect=verify_source), \
             patch.object(native, "command", side_effect=command), \
             patch.object(native, "run_stages", side_effect=lambda *_: events.append("stages")):
            if blocked:
                with self.assertRaisesRegex(RuntimeError, "bootstrap failed"):
                    native.main()
            elif smoke_failed:
                with self.assertRaisesRegex(RuntimeError, "smoke regression failed"):
                    native.main()
            elif c2_failed or source_failed or healthy_failed:
                with self.assertRaisesRegex(RuntimeError, "c2 regression failed|missing mpvqt source|healthy copy failed"):
                    native.main()
            else:
                native.main()
        return events

    def test_bootstrap_before_required_cmake_check(self):
        self.assertEqual(self.exercise(False), ["portable", "native-prereq-inventory", "bootstrap", "verify-mpvqt-source", "shared-patch-tests", "c2-patch-tests", "fixture-copy-tests", "healthy-copy-tests", "windows-smoke-result-tests",
                                                "explicit-mpv-cmake-tests", "linux-sdk-cmake-tests", "windows-mpv-runtime-configure-tests", "stages"])

    def test_existing_cmake_still_runs_required_check(self):
        self.assertEqual(self.exercise(True), ["portable", "native-prereq-inventory",
                                               "verify-mpvqt-source", "shared-patch-tests", "c2-patch-tests", "fixture-copy-tests", "healthy-copy-tests",
                                               "windows-smoke-result-tests",
                                               "explicit-mpv-cmake-tests", "linux-sdk-cmake-tests", "windows-mpv-runtime-configure-tests", "stages"])

    def test_failed_bootstrap_blocks_check_and_stages(self):
        self.assertEqual(self.exercise(False, blocked=True), ["portable", "native-prereq-inventory", "bootstrap"])

    def test_macos_system_still_runs_required_check(self):
        self.assertEqual(self.exercise(True, system="Darwin", mode="system"),
                         ["portable", "native-prereq-inventory", "verify-mpvqt-source", "shared-patch-tests", "c2-patch-tests", "fixture-copy-tests", "healthy-copy-tests", "windows-smoke-result-tests", "explicit-mpv-cmake-tests", "linux-sdk-cmake-tests", "windows-mpv-runtime-configure-tests", "stages"])

    def test_bootstrapped_node_precedes_required_smoke_test(self):
        self.assertEqual(self.exercise(True, node_available=False),
                         ["portable", "native-prereq-inventory", "bootstrap", "verify-mpvqt-source", "shared-patch-tests", "c2-patch-tests", "fixture-copy-tests", "healthy-copy-tests", "windows-smoke-result-tests",
                          "explicit-mpv-cmake-tests", "linux-sdk-cmake-tests", "windows-mpv-runtime-configure-tests", "stages"])

    def test_failed_node_bootstrap_blocks_smoke_test(self):
        self.assertEqual(self.exercise(True, blocked=True, node_available=False),
                         ["portable", "native-prereq-inventory", "bootstrap"])

    def test_failed_smoke_test_blocks_cmake_check_and_stages(self):
        self.assertEqual(self.exercise(True, smoke_failed=True),
                         ["portable", "native-prereq-inventory", "verify-mpvqt-source", "shared-patch-tests", "c2-patch-tests", "fixture-copy-tests", "healthy-copy-tests", "windows-smoke-result-tests"])

    def test_healthy_copy_failure_stops_later_checks(self):
        self.assertEqual(self.exercise(True, healthy_failed=True),
                         ["portable", "native-prereq-inventory", "verify-mpvqt-source", "shared-patch-tests",
                          "c2-patch-tests", "fixture-copy-tests", "healthy-copy-tests"])

    def test_missing_source_and_failed_c2_stop_later_checks(self):
        self.assertEqual(self.exercise(True, source_failed=True),
                         ["portable", "native-prereq-inventory", "verify-mpvqt-source"])
        self.assertEqual(self.exercise(True, c2_failed=True),
                         ["portable", "native-prereq-inventory", "verify-mpvqt-source", "shared-patch-tests", "c2-patch-tests"])

    def test_sdk_pure_checks_are_early_and_cmake_check_is_not(self):
        calls = []
        with patch.object(native, "command", side_effect=lambda name, args, **kwargs: calls.append((name, args))):
            native.portable_checks()
        paths = [args[-1] for _, args in calls]
        self.assertEqual(paths.count('tests/test_windows_loader_driver.py'), 1)
        for path in ("dev/ci/test_linux_sdk_order.py", "dev/ci/test_linux_sdk_inputs.py",
                     "dev/ci/test_qt_sdk_archive.py", "dev/ci/test_linux_qt_plan.py",
                     "dev/ci/test_libarchive_reader_contract.py", "dev/ci/test_linux_qt_pc.py",
                     "dev/ci/test_mac_helpers.py",
                     "dev/ci/test_mac_helpers_acquire.py", "dev/ci/test_mac_carchive.py",
                     "dev/ci/test_qga_terminal.py", "dev/ci/test_macos_sdist_static.py",
                     "dev/ci/test_macos_sdist_supervise.py", "dev/ci/test_macos_sdist_select_launch.py",
                     "dev/ci/test_linux_qt_cache.py",
                     "dev/ci/test_fixture_suspend_policy.py", "dev/ci/test_fixture_suspend_adapter.py",
                     "dev/ci/test_fixture_suspend_linux.py", "dev/ci/test_linux_containment_capability.py", "dev/ci/test_linux_capability_preflight.py", "dev/ci/test_linux_synthetic_service.py",
                     "dev/ci/test_linux_synthetic_service_adapter.py", "dev/ci/test_linux_synthetic_service_dbus.py",
                     "dev/ci/provision_dbus_next.py", "dev/ci/test_linux_synthetic_service_real_dbus.py",
                     "dev/ci/test_settlement_failure.py", "dev/ci/test_settlement_candidates.py", "dev/ci/test_settlement_observation.py", "dev/ci/test_provision_jellyfin_web.py",
                     "dev/ci/test_macos_sdist_select.py", "dev/ci/test_portable_proof_assembly.py", "dev/ci/test_portable_proof_root_policy.py", "dev/ci/test_macos_sdist_pins.py",
                     "dev/ci/test_portable_proof_protocol.py", "dev/ci/test_portable_proof_supervisor.py"):
            self.assertEqual(paths.count(path), 1)
        self.assertNotIn("dev/ci/test_linux_sdk_cmake.py", paths)
        self.assertEqual(paths[paths.index("dev/ci/test_fixture_suspend_linux.py") + 1],
                         "dev/ci/test_linux_containment_capability.py")
        self.assertEqual(paths[paths.index("dev/ci/test_linux_containment_capability.py") + 1],
                         "dev/ci/test_linux_capability_preflight.py")
        self.assertEqual(paths[paths.index("dev/ci/test_linux_capability_preflight.py") + 1],
                         "dev/ci/test_linux_synthetic_service.py")
        index = paths.index("dev/ci/test_linux_synthetic_service.py")
        self.assertEqual(paths[index + 1:index + 3], ["dev/ci/test_linux_synthetic_service_adapter.py",
                                                     "dev/ci/test_linux_synthetic_service_dbus.py"])
        self.assertEqual(paths[index + 3:index + 5], ["dev/ci/provision_dbus_next.py",
                                                     "dev/ci/test_linux_synthetic_service_real_dbus.py"])
        self.assertEqual(paths[paths.index("dev/ci/test_settlement_failure.py") + 1],
                         "dev/ci/test_settlement_candidates.py")
        self.assertEqual(paths[paths.index("dev/ci/test_settlement_candidates.py") + 1],
                         "dev/ci/test_settlement_observation.py")
        start = paths.index("dev/ci/test_qga_terminal.py")
        self.assertEqual(paths[start + 1:start + 5], ["dev/ci/test_portable_proof_assembly.py", "dev/ci/test_portable_proof_root_policy.py",
                         "dev/ci/test_portable_proof_protocol.py", "dev/ci/test_portable_proof_supervisor.py"])

    def test_windows_gate_registers_mac_helper_suites_once(self):
        script = (Path(__file__).resolve().parents[2] / "dev/windows/ci.ps1").read_text(encoding="utf-8")
        for path in ("dev/ci/test_mac_helpers.py", "dev/ci/test_mac_helpers_acquire.py",
                     "dev/ci/test_mac_carchive.py", "dev/ci/test_qga_terminal.py",
                     "dev/ci/test_macos_sdist_static.py", "dev/ci/test_macos_sdist_supervise.py", "dev/ci/test_macos_sdist_select_launch.py",
                     "dev/ci/test_linux_qt_cache.py", "dev/ci/test_fixture_suspend_policy.py",
                     "dev/ci/test_fixture_suspend_adapter.py", "dev/ci/test_fixture_suspend_linux.py",
                     "dev/ci/test_linux_containment_capability.py", "dev/ci/test_linux_capability_preflight.py", "dev/ci/test_linux_synthetic_service.py",
                     "dev/ci/test_linux_synthetic_service_adapter.py", "dev/ci/test_linux_synthetic_service_dbus.py",
                     "dev/ci/provision_dbus_next.py", "dev/ci/test_linux_synthetic_service_real_dbus.py",
                     "dev/ci/test_settlement_failure.py", "dev/ci/test_settlement_candidates.py", "dev/ci/test_settlement_observation.py", "dev/ci/test_macos_sdist_select.py", "dev/ci/test_provision_jellyfin_web.py",
                     "dev/ci/test_portable_proof_assembly.py", "dev/ci/test_portable_proof_root_policy.py", "dev/ci/test_macos_sdist_pins.py", "dev/ci/test_portable_proof_protocol.py",
                     "dev/ci/test_portable_proof_supervisor.py"):
            self.assertEqual(script.count(path), 1)
        order = [script.index("dev/ci/test_" + name + ".py") for name in
                 ("qga_terminal", "portable_proof_assembly", "portable_proof_root_policy", "portable_proof_protocol", "portable_proof_supervisor")]
        self.assertEqual(order, sorted(order))
        self.assertLess(script.index("dev/ci/test_fixture_suspend_linux.py"),
                        script.index("dev/ci/test_linux_containment_capability.py"))
        self.assertLess(script.index("dev/ci/test_linux_containment_capability.py"),
                        script.index("dev/ci/test_linux_capability_preflight.py"))
        self.assertLess(script.index("dev/ci/test_linux_capability_preflight.py"),
                        script.index("dev/ci/test_linux_synthetic_service.py"))
        self.assertLess(script.index("dev/ci/test_linux_synthetic_service.py"),
                        script.index("dev/ci/test_linux_synthetic_service_adapter.py"))
        self.assertLess(script.index("dev/ci/test_linux_synthetic_service_adapter.py"),
                        script.index("dev/ci/test_linux_synthetic_service_dbus.py"))
        self.assertLess(script.index("dev/ci/test_linux_synthetic_service_dbus.py"),
                        script.index("dev/ci/provision_dbus_next.py"))
        self.assertLess(script.index("dev/ci/provision_dbus_next.py"),
                        script.index("dev/ci/test_linux_synthetic_service_real_dbus.py"))
        self.assertLess(script.index("dev/ci/test_settlement_failure.py"),
                        script.index("dev/ci/test_settlement_candidates.py"))
        self.assertLess(script.index("dev/ci/test_settlement_candidates.py"),
                        script.index("dev/ci/test_settlement_observation.py"))

    def test_new_portable_suite_failure_stops_following_checks(self):
        for failed in ("windows-loader-driver-tests", "mac-sdist-select-tests", "settlement-failure-tests", "settlement-candidate-tests", "settlement-observation-tests", "fixture-suspend-policy-tests",
                       "fixture-suspend-adapter-tests", "fixture-suspend-linux-tests", "linux-containment-capability-tests", "linux-capability-preflight-tests", "linux-synthetic-service-tests",
                       "linux-synthetic-adapter-tests", "linux-synthetic-dbus-tests", "linux-synthetic-wheel-provision",
                       "linux-synthetic-real-dbus-tests",
                       "qga-terminal-tests", "mac-sdist-static-tests",
                       "mac-sdist-supervisor-tests", "mac-sdist-launcher-tests", "mac-sdist-pins-tests", "linux-qt-cache-tests", "portable-proof-assembly-tests", "portable-proof-root-policy-tests",
                       "portable-proof-protocol-tests", "portable-proof-supervisor-tests"):
            calls = []
            def run(name, args, **kwargs):
                calls.append(name)
                if name == failed:
                    raise RuntimeError("test failure")
            with self.subTest(failed=failed), patch.object(native, "command", side_effect=run):
                with self.assertRaisesRegex(RuntimeError, "test failure"):
                    native.portable_checks()
                self.assertEqual(calls[-1], failed)


if __name__ == "__main__":
    unittest.main()
