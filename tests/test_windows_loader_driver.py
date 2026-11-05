"""Offline checks for bounded timeout evidence, without launching fixtures."""
import subprocess
import io
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import MagicMock, patch

import windows_mpv_loader_integration as driver


class LoaderDriverTests(unittest.TestCase):
    def test_prelaunch_identity_lists_revision_sources_and_case_inputs(self):
        output = io.StringIO()
        with patch.object(driver, 'run', return_value='a' * 40), \
                patch.object(driver, 'digest', return_value='b' * 64), \
                patch.object(Path, 'is_file', return_value=False), redirect_stdout(output):
            driver.fixture_identity(Path('fixture') / 'fallback.exe')
        text = output.getvalue()
        self.assertIn('source_revision=' + 'a' * 40, text)
        self.assertIn('case=fallback.exe executable_sha256=' + 'b' * 64, text)
        self.assertEqual(text.count('fixture source='), len(driver.INPUT_SOURCES))
        self.assertEqual(text.count('sha256=missing'), 3)

    def test_mode_trace_requires_inside_bit_and_exact_restoration(self):
        output = ('fixture before thread=2 process=4\nbefore load mode=3\n'
                  'fixture after thread=2 process=4\n')
        driver.verify_mode_trace(output)
        for changed in (output.replace('mode=3', 'mode=2'), output.replace('thread=2 process=4\n',
                                                                        'thread=3 process=4\n', 1),
                        output.replace('process=4\n', 'process=5\n', 1)):
            with self.subTest(changed=changed), self.assertRaises(RuntimeError):
                driver.verify_mode_trace(changed)

    def test_timeout_retains_bounded_partial_output_and_marks_retention(self):
        root, output, marker = MagicMock(), MagicMock(), MagicMock()
        root.__truediv__.side_effect = lambda name: {'loader-timeout.txt': output,
                                                     'loader-timeout-pending': marker}[name]
        timeout = subprocess.TimeoutExpired(['fixture'], 30, output=b'x' * 70000)
        with patch.object(driver, 'OUT', root), patch.object(driver.subprocess, 'run', side_effect=timeout):
            with self.assertRaisesRegex(RuntimeError, 'child state at timeout is not established'):
                driver.run(['fixture'])
        output.write_bytes.assert_called_once_with(b'x' * 65536)
        marker.touch.assert_called_once()


if __name__ == '__main__':
    unittest.main()
