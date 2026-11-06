"""Exercise Windows runtime staging configuration without running an application."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / 'CMakeModules/WindowsMpvRuntime.cmake'


class RuntimeConfigureTest(unittest.TestCase):
    def configure(self, application, missing=None):
        (ROOT / 'deps').mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='w589-runtime-', dir=ROOT / 'deps') as temporary:
            base = Path(temporary)
            (base / 'main.cpp').write_text('int main() { return 0; }\n', encoding='utf-8')
            lines = ['cmake_minimum_required(VERSION 3.10...3.31 FATAL_ERROR)', 'project(runtime LANGUAGES CXX)',
                     'set(MAIN_TARGET app)', f'set(MPV_LIBRARY "{(base / "mpv/libmpv.lib").as_posix()}")',
                     'add_executable(test_log main.cpp)',
                     f'set_target_properties(test_log PROPERTIES RUNTIME_OUTPUT_DIRECTORY "{(base / "tests").as_posix()}")']
            for name in ('test_systemcomponent', 'test_settings', 'test_displaymanager',
                         'test_windowmanager', 'test_log_rotation'):
                if name != missing:
                    lines.append(f'add_custom_target({name})')
            if application:
                lines += ['add_executable(app main.cpp)',
                          f'set_target_properties(app PROPERTIES RUNTIME_OUTPUT_DIRECTORY "{(base / "app").as_posix()}")']
            lines.append(f'include("{MODULE.as_posix()}")')
            (base / 'CMakeLists.txt').write_text('\n'.join(lines), encoding='utf-8')
            result = subprocess.run(['cmake', '-S', str(base), '-B', str(base / 'build'), '-G', 'Ninja'],
                                    capture_output=True, text=True, timeout=30, check=False,
                                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
            ninja = (base / 'build/build.ninja').read_text(encoding='utf-8') if result.returncode == 0 else ''
            return result.returncode, ninja, result.stdout + result.stderr

    def test_application_and_diagnostic_layouts(self):
        for present in (False, True):
            with self.subTest(application=present):
                code, ninja, output = self.configure(present)
                self.assertEqual(code, 0, output)
                self.assertIn('/tests/libmpv-2.dll', ninja.replace('\\', '/'))
                self.assertEqual('/app/libmpv-2.dll' in ninja.replace('\\', '/'), present)

    def test_missing_required_consumer_fails(self):
        code, _, output = self.configure(False, missing='test_settings')
        self.assertNotEqual(code, 0)
        self.assertIn('Cannot add target-level dependencies to non-existent target', output)
        self.assertIn('test_settings', output)


if __name__ == '__main__':
    unittest.main()
