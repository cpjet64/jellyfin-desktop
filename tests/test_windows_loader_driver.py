"""Offline checks for bounded timeout evidence, without launching fixtures."""
import subprocess
import io
import unittest
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import MagicMock, patch

import windows_mpv_loader_integration as driver


def failure_trace(policy, error=193):
    lines = ['fixture before thread=2 process=4', 'delay hook variant=fallback',
             'mode before=2 requested=3', 'mode enter requested=3 ok=1 previous=2 error=0 observed=3',
             'before load mode=3', f'after load loaded={int(policy == 127)} error={error}',
             'mode restore requested=2 ok=1 previous=0 error=0 observed=2']
    if policy == 127:
        lines.extend(['missing export hook', 'before ExitProcess policy=127'])
    else:
        lines.append(f'before ExitProcess policy=126 loader_error={error}')
    return '\n'.join(lines) + '\n'


class LoaderDriverTests(unittest.TestCase):
    def test_failure_traces_require_load_restore_and_matching_policy(self):
        for policy, error in ((126, 193), (126, 2), (127, 0), (127, 88)):
            with self.subTest(policy=policy, error=error):
                driver.verify_failure_trace(failure_trace(policy, error), policy)
        invalid = [failure_trace(126, 0), failure_trace(126).replace('ok=1 previous=2', 'ok=0 previous=2'),
                   failure_trace(126).replace('mode restore requested=2 ok=1', 'mode restore requested=2 ok=0'),
                   failure_trace(126).replace('observed=2\n', 'observed=3\n'),
                   failure_trace(126).replace('loaded=0', 'loaded=1'),
                   failure_trace(126).replace('loader_error=193', 'loader_error=126'),
                   failure_trace(126).replace('before load mode=3\n', ''),
                   failure_trace(126).replace('before load mode=3\n', 'before load mode=3\nbefore load mode=3\n'),
                   failure_trace(126).replace('after load loaded=0 error=193\nmode restore',
                                              'mode restore requested=2 ok=1 previous=0 error=0 observed=2\nafter load loaded=0 error=193\nmode restore'),
                   failure_trace(126) + 'before ExitProcess policy=126 loader_error=193\n']
        reordered = failure_trace(126).splitlines()
        reordered[4], reordered[5] = reordered[5], reordered[4]
        invalid.append('\n'.join(reordered))
        for output in invalid:
            with self.subTest(output=output), self.assertRaises(RuntimeError):
                driver.verify_failure_trace(output, 126)
        for output in (failure_trace(127).replace('loaded=1', 'loaded=0'),
                       failure_trace(127).replace('missing export hook\n', ''),
                       failure_trace(127).replace('policy=127', 'policy=126')):
            with self.subTest(output=output), self.assertRaises(RuntimeError):
                driver.verify_failure_trace(output, 127)

    def test_run_checks_failure_trace_not_exit_code_alone(self):
        result = MagicMock(returncode=126, stdout='before ExitProcess policy=126 loader_error=193\n')
        with patch.object(driver, 'fixture_identity'), patch.object(driver.subprocess, 'run', return_value=result), \
                redirect_stdout(io.StringIO()), self.assertRaises(RuntimeError):
            driver.run(['fallback.exe'], 126)

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
