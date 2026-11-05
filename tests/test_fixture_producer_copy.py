"""Offline checks of the fixture-only copy manifest and fail-closed modes."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MPVQT = Path(os.environ.get('MPVQT_TEST_SOURCE', ROOT / 'external/mpvqt'))


class FixtureProducerCopyTest(unittest.TestCase):
    def setUp(self):
        (ROOT / 'deps').mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='fixture-copy-', dir=ROOT / 'deps')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        shutil.copytree(MPVQT / 'src', self.root / 'external/mpvqt/src')
        shutil.copytree(ROOT / 'src/player', self.root / 'src/player')
        (self.root / 'tests').mkdir()
        shutil.copy2(ROOT / 'tests/test_webclient_containment.cpp', self.root / 'tests')
        (self.root / 'patches/fixture-producer').mkdir(parents=True)
        shutil.copy2(ROOT / 'patches/fixture-producer/0001-producer-binding.patch', self.root / 'patches/fixture-producer')

    def configure(self, *options):
        args = ['cmake', '-DPLAYER_INTENT_DIAGNOSTIC_ONLY=ON', '-DUSE_STATIC_MPVQT=ON',
                '-DMPVQT_EXTERNAL_EVENTS=OFF', '-DMPVQT_C2_DIAGNOSTIC_ONLY=OFF',
                *options, '-P', str(ROOT / 'CMakeModules/FixtureProducerCopy.cmake')]
        return subprocess.run(args, cwd=self.root, capture_output=True, text=True, timeout=30, check=False,
                              creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)

    def test_copy_patch_and_output(self):
        result = self.configure()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        copied = self.root / 'fixture-producer-copy'
        item = (copied / 'mpvqt/mpvabstractitem.cpp').read_text(encoding='utf-8')
        player = (copied / 'player/PlayerMpvEvents.cpp').read_text(encoding='utf-8')
        fixture = (copied / 'tests/test_webclient_containment.cpp').read_text(encoding='utf-8')
        self.assertIn('setFixtureWakeOwner(owner)', item)
        self.assertIn('controllerWorkAllowed()', item)
        self.assertIn('fixtureWakeOwner()->handoff()', player)
        self.assertIn('HELD: fixture owner retention', fixture)
        again = self.configure()
        self.assertEqual(again.returncode, 0, again.stdout + again.stderr)

    def test_actual_copy_binding_and_fail_closed_hold(self):
        result = self.configure()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        copied = self.root / 'fixture-producer-copy'
        item = (copied / 'mpvqt/mpvabstractitem.cpp').read_text(encoding='utf-8')
        controller = (copied / 'mpvqt/mpvcontroller.cpp').read_text(encoding='utf-8')
        video = (copied / 'player/MpvVideoItem.cpp').read_text(encoding='utf-8')
        player = (copied / 'player/PlayerMpvEvents.cpp').read_text(encoding='utf-8')
        fixture = (copied / 'tests/test_webclient_containment.cpp').read_text(encoding='utf-8')
        self.assertLess(item.index('setFixtureWakeOwner(owner)'), item.index('&MpvController::init'))
        init = controller.split('void MpvController::init()', 1)[1]
        self.assertLess(init.index('if (!m_fixtureWakeOwner)'), init.index('mpv_create()'))
        self.assertIn('return d_ptr ? d_ptr->m_mpv : nullptr;', controller)
        self.assertLess(init.index('installController(this)'), init.index('setProperty('))
        self.assertIn('if (!fixtureControllerReady()) return;', video)
        self.assertIn('m_initialized = fixturePlayerReady();', video)
        self.assertEqual(player.count('fixtureWakeOwner()->handoff()'), 1)
        self.assertNotIn('emit onMpvEvents()', player)
        self.assertIn('fixtureWakeOwner()->postHook(pause, resume)', player)
        containment = fixture.split('void TestWebClientContainment::containment()', 1)[1].split('int main(', 1)[0]
        self.assertLess(containment.index('HELD: fixture owner retention'), containment.index('new FixtureWakeOwner'))
        self.assertLess(containment.index('new FixtureWakeOwner'), containment.index('new MpvVideoItem'))
        self.assertIn('QVERIFY(video->fixtureControllerReady())', containment)
        self.assertIn('QTRY_VERIFY_WITH_TIMEOUT(video->fixturePlayerReady()', containment)

    def test_alternate_graph_is_declared_without_ordinary_core(self):
        external = (ROOT / 'external/CMakeLists.txt').read_text(encoding='utf-8')
        core = (ROOT / 'src/CMakeLists.txt').read_text(encoding='utf-8')
        tests = (ROOT / 'tests/CMakeLists.txt').read_text(encoding='utf-8')
        self.assertIn('add_library(FixtureMpvQt STATIC', external)
        self.assertIn('mpvrenderer-patched-wrapper.cpp.in', external)
        self.assertIn('add_library(FixtureCore STATIC ${_fixture_core_sources})', core)
        self.assertIn('list(REMOVE_ITEM _fixture_links MpvQt)', core)
        self.assertIn('target_link_libraries(FixtureCore ${_fixture_links} FixtureMpvQt)', core)
        self.assertIn('set(_containment_core FixtureCore)', tests)
        self.assertIn('set(_containment_source "${_fixture_copy}/tests/test_webclient_containment.cpp")', tests)

    def test_missing_changed_and_incompatible_inputs_fail(self):
        missing = self.root / 'external/mpvqt/src/mpvcontroller.cpp'
        original = missing.read_bytes()
        missing.unlink()
        self.assertNotEqual(self.configure().returncode, 0)
        missing.write_bytes(original + b'\n// changed\n')
        self.assertNotEqual(self.configure().returncode, 0)
        missing.write_bytes(original)
        self.assertNotEqual(self.configure('-DMPVQT_EXTERNAL_EVENTS=ON').returncode, 0)
        self.assertNotEqual(self.configure('-DUSE_STATIC_MPVQT=OFF').returncode, 0)
        self.assertNotEqual(self.configure('-DMPVQT_C2_DIAGNOSTIC_ONLY=ON').returncode, 0)

    def test_modified_patch_fails(self):
        patch = self.root / 'patches/fixture-producer/0001-producer-binding.patch'
        patch.write_bytes(patch.read_bytes() + b'\n')
        self.assertNotEqual(self.configure().returncode, 0)


if __name__ == '__main__':
    unittest.main()
