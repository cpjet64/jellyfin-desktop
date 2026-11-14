"""Tests for the actual producer plan; not executed in the source-only task."""

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("producer_plan", ROOT / "dev/ci/recovery_mpv_producer/plan.py")
plan = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = plan
SPEC.loader.exec_module(plan)


class PlanTests(unittest.TestCase):
    def test_order_and_variant_separation(self):
        names = [phase.name for phase in plan.phase_plan()]
        self.assertLess(names.index("validate-identities"), names.index("configure-baseline"))
        self.assertLess(names.index("build-baseline"), names.index("configure-v3"))
        self.assertEqual(len(set(phase.output for phase in plan.phase_plan())), len(names))
        self.assertTrue(all(phase.deadline_seconds > 0 for phase in plan.phase_plan()))

    def test_layout_rejects_overlap_and_existing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            inputs = root / "inputs"
            inputs.mkdir()
            with self.assertRaises(ValueError):
                plan.validate_layout(root, inputs, inputs / "out")
            with self.assertRaises(ValueError):
                plan.validate_layout(root, inputs, inputs)
            self.assertEqual(plan.validate_layout(root, inputs, root / "out")[2], root / "out")

    def test_pins_are_fixed(self):
        self.assertEqual(len(plan.PATCHES), 3)
        self.assertEqual(len(plan.RUST_COMPONENTS), 4)
        self.assertEqual(len(plan.PROVIDER), 40)
        self.assertEqual(len(plan.MPV), 40)
