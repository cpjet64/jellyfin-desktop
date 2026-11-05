"""Portable decision tests; no guest, manager, or cgroup operations."""
import unittest
from unittest.mock import MagicMock, Mock, patch

import linux_containment_capability as capability


class CapabilityTest(unittest.TestCase):
    def test_command_expired_does_not_launch(self):
        with patch.object(capability.time, 'monotonic', return_value=10), \
                patch.object(capability.subprocess, 'Popen') as launch:
            with self.assertRaises(TimeoutError):
                capability.command(['/fixed'], 10)
            launch.assert_not_called()

    def test_failed_launch_is_reported(self):
        with patch.object(capability.time, 'monotonic', return_value=1), \
                patch.object(capability.subprocess, 'Popen', side_effect=FileNotFoundError):
            self.assertEqual(capability.command(['/fixed'], 2), {'error': 'FileNotFoundError'})

    def test_timeout_kills_and_reaps_only_started_child(self):
        process = Mock()
        process.poll.return_value = None
        selector = MagicMock()
        selector.__enter__ = Mock(return_value=selector)
        selector.__exit__ = Mock(return_value=False)
        selector.get_map.return_value = {1: True}
        with patch.object(capability.time, 'monotonic', side_effect=[1, 1, 3]), \
                patch.object(capability.subprocess, 'Popen', return_value=process), \
                patch.object(capability.selectors, 'DefaultSelector', return_value=selector):
            self.assertEqual(capability.command(['/fixed'], 4), {'error': 'TimeoutError'})
            process.kill.assert_called_once()
            process.wait.assert_called_once_with(timeout=2)

    def test_output_limit_kills_owned_child(self):
        process = Mock()
        process.poll.return_value = None
        selector = MagicMock()
        selector.__enter__.return_value = selector
        selector.get_map.return_value = {1: True}
        key = Mock(data='stdout')
        selector.select.return_value = [(key, 1)]
        with patch.object(capability.time, 'monotonic', return_value=1), \
                patch.object(capability.subprocess, 'Popen', return_value=process), \
                patch.object(capability.selectors, 'DefaultSelector', return_value=selector), \
                patch.object(capability.os, 'read', return_value=b'x' * (capability.MAX_OUTPUT + 1)):
            self.assertEqual(capability.command(['/fixed'], 5), {'error': 'ValueError'})
            process.kill.assert_called_once()
            process.wait.assert_called_once_with(timeout=2)

    def test_failed_kill_still_waits_and_closes_both_streams(self):
        process = Mock()
        process.poll.return_value = None
        process.kill.side_effect = OSError('denied')
        selector = MagicMock()
        selector.__enter__.return_value = selector
        selector.get_map.return_value = {1: True}
        with patch.object(capability.time, 'monotonic', side_effect=[1, 1, 3]), \
                patch.object(capability.subprocess, 'Popen', return_value=process), \
                patch.object(capability.selectors, 'DefaultSelector', return_value=selector):
            with self.assertRaises(capability.InspectionCleanupError) as raised:
                capability.command(['/fixed'], 4)
            self.assertEqual(raised.exception.primary, 'TimeoutError')
            self.assertEqual(raised.exception.cleanup, ('OSError',))
            process.wait.assert_called_once()
            process.stdout.close.assert_called_once()
            process.stderr.close.assert_called_once()

    def test_clock_failure_after_launch_cleans_owned_child(self):
        process = Mock()
        process.poll.return_value = None
        with patch.object(capability.time, 'monotonic', side_effect=[1, KeyboardInterrupt()]), \
                patch.object(capability.subprocess, 'Popen', return_value=process):
            with self.assertRaises(KeyboardInterrupt):
                capability.command(['/fixed'], 4)
            process.kill.assert_called_once()
            process.wait.assert_called_once()
            process.stdout.close.assert_called_once()
            process.stderr.close.assert_called_once()

    def test_cleanup_interruptions_preserve_first_failure_and_attempt_all(self):
        process = Mock()
        process.poll.return_value = None
        process.kill.side_effect = KeyboardInterrupt()
        process.wait.side_effect = SystemExit()
        process.stdout.close.side_effect = KeyboardInterrupt()
        with patch.object(capability.time, 'monotonic', side_effect=[1, RuntimeError()]), \
                patch.object(capability.subprocess, 'Popen', return_value=process):
            with self.assertRaises(capability.InspectionCleanupError) as raised:
                capability.command(['/fixed'], 4)
            self.assertEqual(raised.exception.primary, 'RuntimeError')
            self.assertEqual(raised.exception.cleanup, ('KeyboardInterrupt', 'SystemExit', 'KeyboardInterrupt'))
            process.stderr.close.assert_called_once()

    def test_original_value_error_survives_cleanup_cancellation(self):
        process = Mock()
        process.poll.return_value = None
        process.kill.side_effect = KeyboardInterrupt()
        with patch.object(capability.time, 'monotonic', side_effect=[1, ValueError('command')]), \
                patch.object(capability.subprocess, 'Popen', return_value=process):
            with self.assertRaises(capability.InspectionCleanupError) as raised:
                capability.command(['/fixed'], 4)
        self.assertEqual(raised.exception.primary, 'ValueError')
        self.assertEqual(raised.exception.cleanup, ('KeyboardInterrupt',))
        process.wait.assert_called_once()
        process.stdout.close.assert_called_once()
        process.stderr.close.assert_called_once()

    def test_nonzero_exit_survives_cleanup_cancellation(self):
        process = Mock()
        process.poll.side_effect = KeyboardInterrupt()
        process.wait.return_value = 19
        selector = MagicMock()
        selector.__enter__.return_value = selector
        selector.get_map.return_value = {}
        with patch.object(capability.time, 'monotonic', return_value=1), \
                patch.object(capability.subprocess, 'Popen', return_value=process), \
                patch.object(capability.selectors, 'DefaultSelector', return_value=selector):
            with self.assertRaises(capability.InspectionCleanupError) as raised:
                capability.command(['/fixed'], 4)
        self.assertEqual(raised.exception.primary, 'exit=19')
        self.assertEqual(raised.exception.cleanup, ('KeyboardInterrupt',))
        self.assertIsInstance(raised.exception.__cause__, KeyboardInterrupt)
        process.stdout.close.assert_called_once()
        process.stderr.close.assert_called_once()

    def test_uncertain_cleanup_stops_later_context_commands(self):
        for failure in ('kill', 'wait', 'close'):
            with self.subTest(failure=failure):
                process = Mock()
                process.poll.return_value = None
                if failure == 'kill':
                    process.kill.side_effect = OSError('kill')
                elif failure == 'wait':
                    process.wait.side_effect = capability.subprocess.TimeoutExpired('owned', 2)
                else:
                    process.stdout.close.side_effect = OSError('close')
                selector = MagicMock()
                selector.__enter__.return_value = selector
                selector.get_map.return_value = {1: True}
                with patch.object(capability.os, 'geteuid', return_value=100, create=True), \
                     patch.object(capability.os, 'getuid', return_value=100, create=True), \
                     patch.object(capability.os, 'getgid', return_value=100, create=True), \
                     patch.object(capability.os, 'getegid', return_value=100, create=True), \
                     patch.object(capability, 'read', side_effect=['0::/runner\n',
                         '1 0 0:1 / /sys/fs/cgroup rw - cgroup2 cgroup rw']), \
                     patch.object(capability, 'ancestors', return_value=[]), \
                     patch.object(capability.time, 'monotonic', side_effect=[1, 1, 3]), \
                     patch.object(capability.subprocess, 'Popen', return_value=process) as launch, \
                     patch.object(capability.selectors, 'DefaultSelector', return_value=selector):
                    with self.assertRaises(capability.InspectionCleanupError) as raised:
                        capability.inspect_context(100, 4)
                self.assertEqual(raised.exception.primary, 'TimeoutError')
                self.assertEqual(len(raised.exception.cleanup), 1)
                launch.assert_called_once()
                process.stderr.close.assert_called_once()

    def test_overall_deadline_propagates_after_cleanup(self):
        process = Mock()
        process.poll.return_value = None
        with patch.object(capability.time, 'monotonic', side_effect=[1, capability.OverallDeadline('deadline')]), \
                patch.object(capability.subprocess, 'Popen', return_value=process):
            with self.assertRaises(capability.OverallDeadline):
                capability.command(['/fixed'], 4)
        process.kill.assert_called_once()
        process.wait.assert_called_once()
        process.stdout.close.assert_called_once()
        process.stderr.close.assert_called_once()

    def test_cancellation_prevents_next_inspection_command(self):
        calls = []
        def command(args, deadline):
            calls.append(args)
            raise KeyboardInterrupt()
        with patch.object(capability.os, 'geteuid', return_value=100, create=True), \
                patch.object(capability.os, 'getuid', return_value=100, create=True), \
                patch.object(capability.os, 'getgid', return_value=100, create=True), \
                patch.object(capability.os, 'getegid', return_value=100, create=True), \
                patch.object(capability, 'read', side_effect=['0::/runner\n',
                    '1 0 0:1 / /sys/fs/cgroup rw - cgroup2 cgroup rw']), \
                patch.object(capability, 'ancestors', return_value=[]), \
                patch.object(capability, 'command', side_effect=command):
            with self.assertRaises(KeyboardInterrupt):
                capability.inspect_context(100, 10)
        self.assertEqual(len(calls), 1)

    def test_identity_mismatch_stops_before_reads(self):
        with patch.object(capability.os, 'geteuid', return_value=100, create=True), \
                patch.object(capability.os, 'getuid', return_value=101, create=True), \
                patch.object(capability, 'read') as reader:
            with self.assertRaises(ValueError):
                capability.inspect_context(100, 10)
            reader.assert_not_called()

    def test_bind_subtree_mount_is_rejected(self):
        values = {'/proc/self/cgroup': '0::/runner\n',
                  '/proc/self/mountinfo': '1 0 0:1 /sub /sys/fs/cgroup rw - cgroup2 cgroup rw'}
        with patch.object(capability.os, 'geteuid', return_value=100, create=True), \
                patch.object(capability.os, 'getuid', return_value=100, create=True), \
                patch.object(capability.os, 'getgid', return_value=100, create=True), \
                patch.object(capability.os, 'getegid', return_value=100, create=True), \
                patch.object(capability, 'read', side_effect=lambda path, *_: values[path]), \
                patch.object(capability, 'ancestors') as parents:
            with self.assertRaises(ValueError):
                capability.inspect_context(100, 10)
            parents.assert_not_called()

    def test_role_identity_must_match_before_inspection(self):
        with patch.object(capability.os, 'geteuid', return_value=99, create=True), \
                patch.object(capability, 'read') as read:
            with self.assertRaises(ValueError):
                capability.inspect_context(100, 10)
            read.assert_not_called()

    def test_ambiguous_mount_is_rejected(self):
        reads = ['/proc/self/cgroup', '/proc/self/mountinfo']
        values = {'/proc/self/cgroup': '0::/runner\n', '/proc/self/mountinfo': ''}
        with patch.object(capability.os, 'geteuid', return_value=100, create=True), \
                patch.object(capability.os, 'getuid', return_value=100, create=True), \
                patch.object(capability.os, 'getgid', return_value=100, create=True), \
                patch.object(capability.os, 'getegid', return_value=100, create=True), \
                patch.object(capability, 'read', side_effect=lambda path, *_: values[path]) as reader:
            with self.assertRaises(ValueError):
                capability.inspect_context(100, 10)
            self.assertEqual([call.args[0] for call in reader.call_args_list], reads)

    def test_ambiguous_membership_is_rejected(self):
        with patch.object(capability.os, 'geteuid', return_value=100, create=True), \
                patch.object(capability.os, 'getuid', return_value=100, create=True), \
                patch.object(capability.os, 'getgid', return_value=100, create=True), \
                patch.object(capability.os, 'getegid', return_value=100, create=True), \
                patch.object(capability, 'read', return_value='0::/a\n0::/b\n'):
            with self.assertRaises(ValueError):
                capability.inspect_context(100, 10)


if __name__ == '__main__':
    unittest.main()
