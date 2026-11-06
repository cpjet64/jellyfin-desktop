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
           'patches/healthy-restart/0002-player-event-order.patch')


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


if __name__ == '__main__':
    unittest.main()
