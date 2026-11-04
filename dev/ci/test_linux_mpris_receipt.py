"""The unsafe legacy native producer and its receipts are held."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock

import acceptance
import linux_mpris_receipt as producer

ROOT = Path(__file__).resolve().parents[2]


class LinuxMprisHoldTest(unittest.TestCase):
    def test_no_scenario_or_process_is_launched_and_no_receipt_created(self):
        with tempfile.TemporaryDirectory(prefix="mpris-hold-test-", dir=ROOT) as temp:
            root = Path(temp)
            launch = Mock()
            for scenario in ("playing", "paused"):
                with self.subTest(scenario=scenario), self.assertRaisesRegex(RuntimeError, "render-thread contract"):
                    producer.run_scenario(root, root, root, launch, scenario)
            with self.assertRaisesRegex(RuntimeError, "render-thread contract"):
                producer.run_active_stop(root, root, root, launch)
            launch.assert_not_called()
            self.assertFalse((root / "linux-mpris-active-stop").exists())

    def test_legacy_mpris_receipt_cannot_pass(self):
        with self.assertRaisesRegex(ValueError, "legacy MPRIS offscreen receipts are invalidated"):
            acceptance.validate_linux_active_stop(ROOT, {})

    def test_blocked_obligation_records_reason(self):
        with tempfile.TemporaryDirectory(prefix="mpris-hold-test-", dir=ROOT) as temp:
            path = Path(temp) / "evidence.json"
            path.write_text(json.dumps({"obligations": {"linux_active_stop": {}}}))
            producer.update_obligation(path.parent, "blocked", "render-thread contract unresolved")
            result = json.loads(path.read_text())
            self.assertEqual(result["obligations"]["linux_active_stop"],
                             {"status": "blocked", "reason": "render-thread contract unresolved"})


if __name__ == "__main__":
    unittest.main()
