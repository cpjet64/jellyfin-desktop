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
COMMAND_SPEC = importlib.util.spec_from_file_location("producer_commands", ROOT / "dev/ci/recovery_mpv_producer/commands.py")
commands = importlib.util.module_from_spec(COMMAND_SPEC)
sys.modules[COMMAND_SPEC.name] = commands
COMMAND_SPEC.loader.exec_module(commands)


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

    def test_commands_keep_separate_variants_and_fixed_target(self):
        phases = commands.commands(Path("/job"), Path("/tools/cmake"), Path("/tools/ninja"))
        self.assertEqual([item.phase for item in phases],
                         ["configure-baseline", "build-baseline", "configure-v3", "build-v3"])
        self.assertIn("-DGCC_ARCH=x86-64", phases[0].argv)
        self.assertIn("-DGCC_ARCH=x86-64-v3", phases[2].argv)
        self.assertEqual(phases[1].argv[-2:], ("--target", "mpv"))
        self.assertIn("/job/build/baseline", phases[0].argv)
        self.assertIn("/job/build/v3", phases[2].argv)

    def test_environment_is_offline_and_does_not_copy_parent(self):
        env = commands.child_environment("/job", "/job/tools/bin")
        self.assertEqual(env["CARGO_NET_OFFLINE"], "true")
        self.assertEqual(env["MESON_WRAP_MODE"], "nodownload")
        self.assertNotIn("HTTP_PROXY", env)
