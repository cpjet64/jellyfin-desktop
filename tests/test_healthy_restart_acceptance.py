"""Independent checks of the actual healthy copy and its ON-mode event order."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

WORKTREE = Path(__file__).resolve().parents[1]
ROOT = Path(os.environ.get('HEALTHY_TEST_SOURCE', WORKTREE)).resolve()
HELPER = ROOT / 'CMakeModules' / 'HealthyRestartCopy.cmake'
PINS = ROOT / 'CMakeModules' / 'HealthyRestartPins.cmake'
MPVQT = Path(os.environ.get('MPVQT_TEST_SOURCE', ROOT / 'external/mpvqt'))
FLAGS = subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0
OPTIONS = {'HEALTHY_RESTART_DIAGNOSTIC_ONLY': 'ON', 'PLAYER_INTENT_DIAGNOSTIC_ONLY': 'ON',
           'USE_STATIC_MPVQT': 'ON', 'MPVQT_EXTERNAL_EVENTS': 'ON',
           'MPVQT_C2_DIAGNOSTIC_ONLY': 'OFF', 'FIXTURE_PRODUCER_COPY_ONLY': 'OFF'}
PATCHES = ('patches/mpvqt/0001-event-ownership.patch',
           'patches/healthy-restart/0001-render-capture.patch',
           'patches/healthy-restart/0002-player-event-order.patch',
           'patches/healthy-restart/0003-render-retirement.patch',
           'patches/healthy-restart/0004-async-teardown.patch',
           'patches/healthy-restart/0005-async-stop.patch')


class HealthyRestartAcceptance(unittest.TestCase):
    def setUp(self):
        base = WORKTREE / 'deps'
        base.mkdir(exist_ok=True)
        if base.is_symlink() or base.resolve() != base:
            raise RuntimeError('test storage must remain inside the worktree')
        self.temp = tempfile.TemporaryDirectory(prefix='healthy-acceptance-', dir=base)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for module in (HELPER, PINS):
            self.copy(module, Path('CMakeModules') / module.name)
        text = PINS.read_text(encoding='utf-8')
        entries = re.findall(r'"([^"|]+)\|([^"|]+)\|([0-9a-f]{64})"', text)
        self.assertGreater(len(entries), 50)
        for source, _, _ in entries:
            path = Path(source)
            actual = MPVQT / Path(*path.parts[2:]) if path.parts[:2] == ('external', 'mpvqt') else ROOT / path
            self.copy(actual, path)
        for path in PATCHES:
            self.copy(ROOT / path, Path(path))
        (self.root / 'check.cmake').write_text('include("${CMAKE_SOURCE_DIR}/CMakeModules/HealthyRestartCopy.cmake")\n', encoding='utf-8')

    def copy(self, source, relative):
        target = self.root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)

    def run_copy(self, **overrides):
        options = dict(OPTIONS, **overrides)
        args = ['cmake', *(f'-D{k}={v}' for k, v in options.items()), '-P', 'check.cmake']
        return subprocess.run(args, cwd=self.root, capture_output=True, text=True,
                              timeout=60, creationflags=FLAGS)

    def test_incompatible_configurations_fail_before_copy(self):
        for key, value in OPTIONS.items():
            with self.subTest(option=key):
                opposite = 'OFF' if value == 'ON' else 'ON'
                result = self.run_copy(**{key: opposite})
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((self.root / 'healthy-restart-copy').exists())

    def test_actual_copy_rejects_missing_and_mutated_inputs(self):
        player = self.root / 'src/player/PlayerMpvEvents.cpp'
        original = player.read_bytes()
        player.unlink()
        self.assertNotEqual(self.run_copy().returncode, 0)
        self.assertFalse((self.root / 'healthy-restart-copy').exists())
        player.write_bytes(original + b'\n// altered\n')
        self.assertNotEqual(self.run_copy().returncode, 0)
        self.assertFalse((self.root / 'healthy-restart-copy').exists())

    def test_existing_copy_and_patch_mismatch_fail_closed(self):
        first = self.run_copy()
        self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
        copied = self.root / 'healthy-restart-copy/player/PlayerMpvEvents.cpp'
        original = copied.read_bytes()
        copied.write_bytes(original + b'\n// altered\n')
        self.assertNotEqual(self.run_copy().returncode, 0)
        self.assertEqual(copied.read_bytes(), original + b'\n// altered\n')
        copied.write_bytes(original)
        patch = self.root / PATCHES[-1]
        patch.write_bytes(patch.read_bytes() + b'\n')
        self.assertNotEqual(self.run_copy().returncode, 0)
        self.assertEqual(copied.read_bytes(), original)

    def test_copied_event_owner_and_registration_order(self):
        result = self.run_copy()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        copied = self.root / 'healthy-restart-copy'
        player = (copied / 'player/PlayerMpvEvents.cpp').read_text(encoding='utf-8')
        setup = player.split('void PlayerComponent::setupMpvEvents()', 1)[1].split('void PlayerComponent::finishMpvEventSetup()', 1)[0]
        finish = player.split('void PlayerComponent::finishMpvEventSetup()', 1)[1].split('void PlayerComponent::subscribeMpvSettings()', 1)[0]
        on_branch = setup.split('#else', 1)[0]
        self.assertNotIn('mpv_set_wakeup_callback', on_branch)
        self.assertNotIn('emit onMpvEvents()', on_branch)
        self.assertEqual(finish.count('mpv_set_wakeup_callback'), 1)
        self.assertEqual(finish.count('emit onMpvEvents()'), 1)
        component = (copied / 'player/PlayerComponent.cpp').read_text(encoding='utf-8')
        self.assertLess(component.index('setupMpvEvents()'), component.index('mpv_observe_property'))
        self.assertLess(component.index('mpv_hook_add'), component.index('finishMpvEventSetup()'))
        controller = (copied / 'mpvqt/mpvcontroller.cpp').read_text(encoding='utf-8')
        self.assertRegex(controller, r'if\s*\([^\n]*EventMode::Internal[^\n]*\)\s*\{?\s*mpv_set_wakeup_callback')
        for path in ('mpris/MprisComponent.cpp', 'mpris/MprisComponent.h',
                     'input/apple/InputAppleMediaKeys.h', 'input/apple/InputAppleMediaKeys.mm',
                     'settings/AudioSettingsController.cpp', 'taskbar/TaskbarComponentWin.cpp'):
            self.assertTrue((copied / path).is_file(), path)

    def test_copied_renderer_retirement_is_unqualified_by_default(self):
        result = self.run_copy()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        copied = self.root / 'healthy-restart-copy/mpvqt'
        renderer = (copied / 'mpvrenderer.cpp').read_text(encoding='utf-8')
        create = renderer.split('MpvRenderer::createFramebufferObject(', 1)[1]
        create = create.split('MpvRenderer::installHealthyLifetime(', 1)[0]
        self.assertLess(create.index('slot->current()'), create.index('existing->enterRender'))
        self.assertLess(create.index('slot->beginCreation()'), create.index('installHealthyLifetime'))
        self.assertLess(create.index('installHealthyLifetime'), create.index('mpv_render_context_set_update_callback'))
        lifetime = (copied / 'lifetime.cpp').read_text(encoding='utf-8')
        install = lifetime.split('Slot::install(', 1)[1].split('Slot::request(', 1)[0]
        self.assertIn('Barrier::Unqualified', install)
        self.assertIn('if (barrier_ == Barrier::Unqualified && !moduleQualification_)', lifetime)


class HealthyRestartWrapper(unittest.TestCase):
    def test_owner_sources_compile_once_without_test_flag(self):
        base = WORKTREE / 'deps'
        base.mkdir(exist_ok=True)
        if base.is_symlink() or base.resolve() != base:
            raise RuntimeError('test storage must remain inside the worktree')
        with tempfile.TemporaryDirectory(prefix='healthy-owner-target-', dir=base) as directory:
            work = Path(directory)
            mpvqt = work / 'build/copy/mpvqt'
            mpvqt.mkdir(parents=True)
            for name in ('mpvabstractitem.cpp', 'mpvcontroller.cpp', 'frame_capture.cpp',
                         'frame_acceptance.cpp', 'lifetime.cpp', 'lifetime_qt.cpp',
                         'owner.cpp', 'owner_qt.cpp', 'closure.cpp', 'closure_native.cpp'):
                (mpvqt / name).write_text('// configure-only source\n', encoding='utf-8')
            targets = ROOT / 'CMakeModules/HealthyRestartTargets.cmake'
            project = ('cmake_minimum_required(VERSION 3.20)\nproject(OwnerProbe CXX)\n'
                       f'include("{targets.as_posix()}")\n'
                       'foreach(name IN ITEMS Quick Core Gui)\n'
                       '  add_library(Qt6::${name} INTERFACE IMPORTED)\nendforeach()\n'
                       'add_library(Threads::Threads INTERFACE IMPORTED)\n'
                       'set(_healthy_copy "${CMAKE_BINARY_DIR}/copy")\n'
                       'healthy_restart_mpvqt()\n'
                       'get_target_property(defs HealthyRestartMpvQt COMPILE_DEFINITIONS)\n'
                       'get_target_property(sources HealthyRestartMpvQt SOURCES)\n'
                       'file(WRITE "${CMAKE_BINARY_DIR}/defs.txt" "${defs}")\n'
                       'file(WRITE "${CMAKE_BINARY_DIR}/sources.txt" "${sources}")\n')
            (work / 'CMakeLists.txt').write_text(project, encoding='utf-8')
            result = subprocess.run(['cmake', '-S', str(work), '-B', str(work / 'build')],
                                    cwd=work, capture_output=True, text=True,
                                    timeout=60, creationflags=FLAGS)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            defs = (work / 'build/defs.txt').read_text(encoding='utf-8').split(';')
            sources = (work / 'build/sources.txt').read_text(encoding='utf-8').split(';')
            self.assertNotIn('HEALTHY_RETIREMENT_TESTING', defs)
            for name in ('lifetime.cpp', 'lifetime_qt.cpp', 'owner.cpp', 'owner_qt.cpp'):
                self.assertEqual(sources.count((mpvqt / name).as_posix()), 1, name)

    def test_copied_include_interface_propagates_to_consumer(self):
        base = WORKTREE / 'deps'
        base.mkdir(exist_ok=True)
        if base.is_symlink() or base.resolve() != base:
            raise RuntimeError('test storage must remain inside the worktree')
        with tempfile.TemporaryDirectory(prefix='healthy-interface-', dir=base) as directory:
            work = Path(directory)
            (work / 'src').mkdir()
            (work / 'src/dummy.cpp').write_text('int dummy;\n', encoding='utf-8')
            for path in ('player/PlayerComponent.cpp', 'settings/AudioSettingsController.cpp'):
                copied = work / 'build/copy' / path
                copied.parent.mkdir(parents=True, exist_ok=True)
                copied.write_text('int dummy_copy;\n', encoding='utf-8')
            for name in ('closure_player.cpp', 'closure_wake.cpp', 'closure_wake_native.cpp', 'async_stop.cpp',
                         'async_stop_player.cpp', 'module_identity.cpp', 'module_identity_win.cpp',
                         'module_identity_import.cpp', 'module_import_prepare.cpp', 'module_gate.cpp', 'module_channel.cpp',
                         'module_endpoints.cpp', 'lifetime_module.cpp', 'graph_access.cpp', 'graph_close.cpp',
                         'graph_prepare.cpp', 'graph_retire.cpp', 'graph_retirement.cpp', 'graph_start.cpp',
                         '../fixture_restart/WindowsFrameReader.cpp', '../fixture_restart/WindowsRestartIo.cpp'):
                copied = work / 'build/copy/mpvqt' / name
                copied.parent.mkdir(parents=True, exist_ok=True)
                copied.write_text('// configure-only source\n', encoding='utf-8')
            (work / 'consumer.cpp').write_text('int main() { return 0; }\n', encoding='utf-8')
            targets = ROOT / 'CMakeModules/HealthyRestartTargets.cmake'
            project = ('cmake_minimum_required(VERSION 3.20)\nproject(InterfaceProbe CXX)\n'
                       f'include("{targets.as_posix()}")\n'
                       'set(_healthy_copy "${CMAKE_BINARY_DIR}/copy")\n'
                       'set(_healthy_input_pins "src/player/PlayerComponent.cpp|player/PlayerComponent.cpp|pin" '
                       '"src/settings/AudioSettingsController.cpp|settings/AudioSettingsController.cpp|pin")\n'
                       'set(ALL_SRCS "${CMAKE_SOURCE_DIR}/src/dummy.cpp" '
                       '"${CMAKE_SOURCE_DIR}/src/player/PlayerComponent.cpp" '
                       '"${CMAKE_SOURCE_DIR}/src/settings/AudioSettingsController.cpp")\n'
                       'add_library(jmp_core INTERFACE)\n'
                       'set_property(TARGET jmp_core PROPERTY LINK_LIBRARIES MpvQt)\n'
                       'add_library(HealthyRestartMpvQt INTERFACE)\n'
                       'function(configure_windows_loader)\nendfunction()\n'
                       'healthy_restart_core()\n'
                       'get_target_property(core_sources HealthyRestartCore SOURCES)\n'
                       'get_target_property(core_defs HealthyRestartCore COMPILE_DEFINITIONS)\n'
                       'file(WRITE "${CMAKE_BINARY_DIR}/core-sources.txt" "${core_sources}")\n'
                       'file(WRITE "${CMAKE_BINARY_DIR}/core-defs.txt" "${core_defs}")\n'
                       'get_source_file_property(player_includes "${_healthy_copy}/player/PlayerComponent.cpp" INCLUDE_DIRECTORIES)\n'
                       'file(WRITE "${CMAKE_BINARY_DIR}/player-includes.txt" "${player_includes}")\n'
                       'get_source_file_property(settings_includes "${_healthy_copy}/settings/AudioSettingsController.cpp" INCLUDE_DIRECTORIES)\n'
                       'file(WRITE "${CMAKE_BINARY_DIR}/settings-includes.txt" "${settings_includes}")\n'
                       'add_executable(consumer consumer.cpp)\n'
                       'target_link_libraries(consumer PRIVATE HealthyRestartCore)\n'
                       'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/includes.txt" '
                       'CONTENT "$<TARGET_PROPERTY:consumer,INCLUDE_DIRECTORIES>")\n')
            (work / 'CMakeLists.txt').write_text(project, encoding='utf-8')
            result = subprocess.run(['cmake', '-S', str(work), '-B', str(work / 'build')],
                                    cwd=work, capture_output=True, text=True,
                                    timeout=60, creationflags=FLAGS)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            includes = (work / 'build/includes.txt').read_text(encoding='utf-8').split(';')
            copied = (work / 'build/copy/core').as_posix()
            fallback = (work / 'src').as_posix()
            self.assertIn(copied, includes)
            self.assertIn(fallback, includes)
            self.assertLess(includes.index(copied), includes.index(fallback))
            self.assertNotIn((work / 'src/core').as_posix(), includes)
            self.assertEqual((work / 'build/player-includes.txt').read_text(encoding='utf-8'), 'NOTFOUND')
            local = (work / 'build/settings-includes.txt').read_text(encoding='utf-8').split(';')
            self.assertLess(local.index(copied), local.index((work / 'src/settings').as_posix()))
            sources = (work / 'build/core-sources.txt').read_text(encoding='utf-8').split(';')
            for name in ('closure_player.cpp', 'closure_wake.cpp', 'closure_wake_native.cpp',
                         'async_stop.cpp', 'async_stop_player.cpp', 'module_identity.cpp',
                         'module_identity_win.cpp', 'module_identity_import.cpp', 'module_import_prepare.cpp',
                         'module_gate.cpp', 'module_channel.cpp', 'module_endpoints.cpp', 'lifetime_module.cpp',
                         'graph_access.cpp', 'graph_close.cpp', 'graph_prepare.cpp', 'graph_retire.cpp',
                         'graph_retirement.cpp', 'graph_start.cpp'):
                self.assertEqual(sources.count((work / 'build/copy/mpvqt' / name).as_posix()), 1, name)
            defs = (work / 'build/core-defs.txt').read_text(encoding='utf-8')
            self.assertNotIn('HEALTHY_RETIREMENT_TESTING', defs)
            self.assertNotIn('HEALTHY_MODULE_TESTING', defs)

    def test_all_pinned_player_and_platform_sources_map_to_their_copies(self):
        base = WORKTREE / 'deps'
        base.mkdir(exist_ok=True)
        if base.is_symlink() or base.resolve() != base:
            raise RuntimeError('test storage must remain inside the worktree')
        with tempfile.TemporaryDirectory(prefix='healthy-mapping-', dir=base) as directory:
            work = Path(directory)
            entries = re.findall(r'"(src/[^"|]+)\|([^"|]+)\|[0-9a-f]{64}"',
                                 PINS.read_text(encoding='utf-8'))
            paths = [target for source, target in entries if source == 'src/' + target]
            self.assertGreater(len(paths), 50)
            for path in paths:
                output = work / 'copy' / path
                output.parent.mkdir(parents=True, exist_ok=True)
                output.touch()
            script = work / 'mapping.cmake'
            sources = ';'.join((work / 'src' / path).as_posix() for path in paths)
            other = (work / 'src/other.cpp').as_posix()
            script.write_text('cmake_minimum_required(VERSION 3.20)\n'
                              f'include("{PINS.as_posix()}")\n'
                              f'include("{(ROOT / "CMakeModules/HealthyRestartTargets.cmake").as_posix()}")\n'
                              'set(_healthy_copy "${CMAKE_SOURCE_DIR}/copy")\n'
                              f'set(ALL_SRCS "{sources};{other}")\n'
                              'healthy_restart_source_mapping(result)\n'
                              'file(WRITE "${CMAKE_SOURCE_DIR}/result" "${result}")\n', encoding='utf-8')
            args = ['cmake', '-P', str(script)]
            result = subprocess.run(args, cwd=work, capture_output=True, text=True,
                                    timeout=30, creationflags=FLAGS)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            mapped = (work / 'result').read_text(encoding='utf-8').split(';')
            self.assertEqual(mapped, [(work / 'copy' / path).as_posix() for path in paths] + [other])
            (work / 'copy/core/ComponentManager.h').unlink()
            result = subprocess.run(args, cwd=work, capture_output=True, text=True,
                                    timeout=30, creationflags=FLAGS)
            self.assertNotEqual(result.returncode, 0)

    def test_generated_wrapper_line_endings_and_content(self):
        base = WORKTREE / 'deps'
        base.mkdir(exist_ok=True)
        if base.is_symlink() or base.resolve() != base:
            raise RuntimeError('test storage must remain inside the worktree')
        with tempfile.TemporaryDirectory(prefix='healthy-wrapper-', dir=base) as directory:
            work = Path(directory)
            verifier = ROOT / 'CMakeModules/HealthyRestartTargets.cmake'
            script = work / 'verify.cmake'
            script.write_text(f'include("{verifier.as_posix()}")\n'
                              'healthy_restart_verify_wrapper("${WRAPPER}")\n', encoding='utf-8')
            wrapper = work / 'wrapper.cpp'
            expected = b'#include "qt-wayland-compat.h"\n#include "mpvrenderer.cpp"\n'
            for label, content, valid in (
                ('lf', expected, True),
                ('crlf', expected.replace(b'\n', b'\r\n'), True),
                ('altered', expected.replace(b'mpvrenderer.cpp', b'other.cpp'), False),
            ):
                with self.subTest(label=label):
                    wrapper.write_bytes(content)
                    result = subprocess.run(['cmake', f'-DWRAPPER={wrapper.as_posix()}', '-P', str(script)],
                                            cwd=work, capture_output=True, text=True,
                                            timeout=30, creationflags=FLAGS)
                    self.assertEqual(result.returncode == 0, valid, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
