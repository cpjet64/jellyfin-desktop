"""Compare the registered diagnostic cases with both platform route inventories."""

from pathlib import Path
import re
import unittest

import healthy_diagnostic as diagnostic

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'CMakeModules/HealthyRestartFixtureTests.cmake'
TARGETS = ROOT / 'CMakeModules/HealthyRestartTargets.cmake'
WINDOWS = ROOT / 'dev/windows/healthy-diagnostic-cases.ps1'


def cmake_cases(text, marker):
    block = text.split(marker, 1)[1].split('endforeach()', 1)[0]
    return block.split('foreach(case IN ITEMS', 1)[1].split(')', 1)[0].split()


def windows_cases(text, target):
    if target == 'healthy_healthy_restore_cancel':
        block = text.split("if ($Target -eq 'healthy_healthy_restore_cancel')", 1)[1].split('    }', 1)[0]
        return re.findall(r"'([^']+)'", block)
    if target == 'healthy_player_restore_cleanup':
        block = text.split('    return @(', 2)[2].split(')\n}', 1)[0]
        return re.findall(r"'([^']+)'", block)
    block = text.split("'" + target + "' = @(\n", 1)[1].split('        )', 1)[0]
    return re.findall(r"'([^']+)'", block)


class HealthyCaseInventoryTest(unittest.TestCase):
    def test_fixed_case_groups_match_registration_and_windows(self):
        fixture = FIXTURE.read_text(encoding='utf-8')
        windows = WINDOWS.read_text(encoding='utf-8')
        groups = {
            'healthy_graph_capture': 'function(healthy_restart_capture_cases)',
            'healthy_unpause_player': 'function(healthy_restart_unpause_cases)',
            'healthy_graph_capture_lifetime': 'add_executable(healthy_graph_capture_lifetime',
            'healthy_player_restore_observation': 'function(healthy_restart_observation_cases)',
            'healthy_player_native_restore': 'function(healthy_restart_native_restore_cases)',
            'healthy_graph_dispatch': 'function(healthy_restart_dispatch_cases)',
            'healthy_healthy_restore_cancel': 'function(healthy_restart_cancel_cases)',
            'healthy_player_restore_cleanup': 'function(healthy_restart_cleanup_cases)',
        }
        targets = TARGETS.read_text(encoding='utf-8')
        for target, marker in groups.items():
            with self.subTest(target=target):
                text = targets if target == 'healthy_graph_capture_lifetime' else fixture
                registered = cmake_cases(text, marker)
                expected = diagnostic.CASE_GROUPS[target]
                self.assertEqual(len(registered), len(set(registered)))
                self.assertCountEqual(registered, expected)
                self.assertCountEqual(windows_cases(windows, target), expected)

    def test_windows_only_groups_match_registration(self):
        targets = TARGETS.read_text(encoding='utf-8')
        windows = WINDOWS.read_text(encoding='utf-8')
        for target, count in (('healthy_b_channel_dispatch', 13), ('healthy_graph_arm', 19),
                              ('healthy_graph_failure_provenance', 7)):
            with self.subTest(target=target):
                registered = cmake_cases(targets, 'add_executable(' + target)
                self.assertEqual(len(registered), count)
                self.assertEqual(len(set(registered)), count)
                self.assertCountEqual(windows_cases(windows, target), registered)


if __name__ == '__main__':
    unittest.main()
