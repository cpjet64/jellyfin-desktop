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
        process.poll.return_value = 0
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
        process.poll.return_value = 0
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
                patch.object(capability, 'read', side_effect=lambda path, *_: values[path]) as reader:
            with self.assertRaises(ValueError):
                capability.inspect_context(100, 10)
            self.assertEqual([call.args[0] for call in reader.call_args_list], reads)

    def test_ambiguous_membership_is_rejected(self):
        with patch.object(capability.os, 'geteuid', return_value=100, create=True), \
                patch.object(capability, 'read', return_value='0::/a\n0::/b\n'):
            with self.assertRaises(ValueError):
                capability.inspect_context(100, 10)


if __name__ == '__main__':
    unittest.main()
