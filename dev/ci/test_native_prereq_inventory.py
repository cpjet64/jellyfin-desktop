import unittest
import io
import json
from contextlib import redirect_stdout
from types import SimpleNamespace
from unittest.mock import patch

import native_prereq_inventory as inventory


class InventoryTest(unittest.TestCase):
    def test_candidate_rejects_missing_and_options(self):
        self.assertEqual(inventory.candidate("  Candidate: 2:0.35.1-4+deb12u2\n"), "2:0.35.1-4+deb12u2")
        for value in ("(none)", "--option", "", "bad/path"):
            self.assertIsNone(inventory.candidate("  Candidate: " + value))

    def test_transaction_and_sizes(self):
        text = ("Inst libmpv2 [0.34] (0.35 Debian:stable [amd64])\n"
                "Inst xvfb (2:21.1 Debian:stable [amd64])\nRemv obsolete [1.0]\n")
        with patch.object(inventory, "version_action", return_value="upgrade"):
            items = inventory.transaction(text)
        self.assertEqual([item["action"] for item in items], ["upgrade", "install", "remove"])
        self.assertEqual(items[0]["version"], "0.35")
        self.assertEqual(inventory.package_sizes("Size: 42\nInstalled-Size: 12\n"),
                         {"Size": 42, "Installed-Size": 12288})

    def test_command_allowlist_rejects_mutating_forms(self):
        for command in (["apt-get", "install", "xvfb"], ["apt-get", "update"],
                        ["apt-cache", "policy", "--option"], ["apt-get", "--simulate",
                         "--no-install-recommends", "install", "xvfb=1", "-o"],
                        ["dpkg", "--configure", "-a"], ["dpkg", "--compare-versions", "1", "eq", "2"],
                        ["brew", "update"], ["xcrun", "--run", "sh"],
                        ["stat", "-f", "%u", "/etc/passwd"]):
            with self.subTest(command=command), self.assertRaises(ValueError):
                inventory.run(command)

    def test_expired_budget_never_launches_and_still_checks_allowlist(self):
        with patch.object(inventory, "DEADLINE", 10), patch.object(inventory.time, "monotonic", return_value=11), \
                patch.object(inventory.subprocess, "run") as launch:
            result = inventory.run(["apt-cache", "policy", "libmpv2"])
            self.assertEqual(result["error"], "collector_budget_exhausted")
            with self.assertRaises(ValueError):
                inventory.run(["apt-get", "update"])
            launch.assert_not_called()

    def test_subprocess_timeout_is_capped_by_remaining_budget(self):
        with patch.object(inventory, "DEADLINE", 20), patch.object(inventory.time, "monotonic", return_value=17.5), \
                patch.object(inventory.subprocess, "run", return_value=SimpleNamespace(returncode=0, stdout="")) as launch:
            inventory.run(["apt-cache", "policy", "libmpv2"], timeout=60)
        self.assertEqual(launch.call_args.kwargs["timeout"], 2.5)

    def test_exhaustion_emits_complete_partial_json_without_subprocess(self):
        output = io.StringIO()
        with patch.object(inventory, "DEADLINE", None), patch.object(inventory.time, "monotonic", side_effect=[0] + [600] * 100), \
                patch.object(inventory.platform, "system", return_value="Linux"), \
                patch.object(inventory.platform, "machine", return_value="x86_64"), \
                patch.object(inventory.os, "getuid", return_value=1, create=True), \
                patch.object(inventory.shutil, "disk_usage", return_value=SimpleNamespace(free=1)), \
                patch.object(inventory.shutil, "which", return_value=None), \
                patch.object(inventory, "index_metadata", return_value={}), \
                patch.object(inventory.subprocess, "run") as launch, redirect_stdout(output):
            inventory.main()
        report = json.loads(output.getvalue())
        self.assertTrue(report["budget_exhausted"])
        for group in report["details"]["groups"].values():
            self.assertIn("budget exhausted", group["simulation"]["blocked"])
            self.assertIsNone(group["constrained"]["preserves_installed"])
        launch.assert_not_called()

    def test_exact_candidates_and_missing_metadata_are_honest(self):
        calls = []

        def fake(args, timeout=60):
            calls.append(args)
            if args[:2] == ["apt-cache", "policy"]:
                return {"exit": 0, "output": "  Candidate: 1.2-3\n"}
            if args[0] == "apt-get":
                return {"exit": 0, "output": "Inst xvfb (1.2-3 repo [amd64])\n"}
            return {"exit": 0, "output": ""}

        with patch.object(inventory, "run", side_effect=fake), patch.object(inventory, "index_metadata", return_value={}):
            result = inventory.linux_inventory()
        self.assertIn(["apt-get", "--simulate", "--no-install-recommends", "install", "libmpv2=1.2-3"], calls)
        self.assertIn(["apt-get", "--simulate", "--no-install-recommends", "install", "libmpv-dev=1.2-3", "libmpv2=1.2-3"], calls)
        self.assertIn(["apt-get", "--simulate", "--no-install-recommends", "install", "xvfb=1.2-3", "mesa-utils=1.2-3"], calls)
        self.assertIsNone(result["groups"]["runtime"]["download_bytes_sum"])
        self.assertIsNone(result["groups"]["runtime"]["installed_bytes_sum"])

    def test_missing_candidate_never_simulates(self):
        with patch.object(inventory, "run", return_value={"exit": 0, "output": "Candidate: (none)"}) as call, \
             patch.object(inventory, "index_metadata", return_value={}):
            result = inventory.linux_inventory()
        self.assertTrue(all("blocked" in group["simulation"] for group in result["groups"].values()))
        self.assertFalse(any(args.args[0][0] == "apt-get" for args in call.call_args_list))

    def test_version_classification_is_not_lexicographic(self):
        def compare(args, timeout=60):
            new, op, old = args[2], args[3], args[4]
            values = {"2": 2, "10": 10, "1.0-0": 1, "1.0": 1}
            success = values[new] < values[old] if op == "lt" else values[new] > values[old]
            return {"exit": 0 if success else 1, "output": ""}
        with patch.object(inventory, "run", side_effect=compare):
            self.assertEqual(inventory.version_action("2", "10"), "upgrade")
            self.assertEqual(inventory.version_action("10", "2"), "downgrade")
            self.assertEqual(inventory.version_action("1.0", "1.0-0"), "unchanged")
            self.assertEqual(inventory.version_action("2", "2"), "unchanged")

    def test_transaction_distinguishes_downgrade_equal_and_removal(self):
        data = ("Inst one [2] (1 repo [amd64])\nInst two [1] (1 repo [amd64])\n"
                "Inst three [1] (2 repo [amd64])\nRemv four [4]\n")
        def action(old, new):
            return "downgrade" if new < old else "upgrade" if new > old else "unchanged"
        with patch.object(inventory, "version_action", side_effect=action):
            changes = inventory.transaction(data)
        self.assertEqual([item["action"] for item in changes], ["downgrade", "unchanged", "upgrade", "remove"])
        self.assertEqual([item["installed"] for item in changes], ["2", "1", "1", "4"])

    def test_six_bounded_simulations_preserve_exact_freezes_and_conflicts(self):
        calls = []
        def fake(args, timeout=60):
            calls.append(args)
            if args[:2] == ["apt-cache", "policy"]:
                return {"exit": 0, "output": "  Candidate: 2\n"}
            if args[0] == "dpkg":
                return {"exit": 0 if args[3] == "gt" else 1, "output": ""}
            if args[0] == "apt-get":
                if "shared=1" in args:
                    return {"command": args, "exit": 100, "output": "Conflicts: frozen shared dependency"}
                return {"exit": 0, "output": "Inst shared [1] (2 repo [amd64])\nRemv obsolete [3]\n"}
            return {"exit": 0, "output": "Size: 50\nInstalled-Size: 10\n"}
        with patch.object(inventory, "run", side_effect=fake), patch.object(inventory, "index_metadata", return_value={}):
            result = inventory.linux_inventory()
        simulations = [args for args in calls if args[0] == "apt-get"]
        self.assertEqual(len(simulations), 6)
        for group in result["groups"].values():
            self.assertEqual([item["action"] for item in group["transaction"]], ["upgrade", "remove"])
            constrained = group["constrained"]["simulation"]
            self.assertEqual(constrained["exit"], 100)
            self.assertIn("Conflicts: frozen", constrained["output"])
            self.assertEqual(constrained["command"][-2:], ["obsolete=3", "shared=1"])
            self.assertIsNone(group["constrained"]["preserves_installed"])

    def test_constrained_solver_cannot_hide_newly_introduced_change(self):
        calls = []
        def fake(args, timeout=60):
            calls.append(args)
            if args[0] == "dpkg":
                return {"exit": 0 if args[3] == "gt" else 1}
            if args[0] == "apt-get":
                package = "different" if "original=1" in args else "original"
                return {"command": args, "exit": 0, "output": f"Inst {package} [1] (2 repo [amd64])\n"}
            return {"exit": 0, "output": ""}
        with patch.object(inventory, "run", side_effect=fake):
            group = inventory.solve_group(("libmpv2",), {"libmpv2": "2"}, lambda *_: {"output": ""})
        self.assertFalse(group["constrained"]["preserves_installed"])
        self.assertEqual(sum(args[0] == "apt-get" for args in calls), 2)

    def test_requested_candidate_is_explicitly_overridden_once(self):
        changes = [{"action": "upgrade", "package": "libmpv2", "installed": "1", "version": "2"}]
        operands, overrides = inventory.constrained_operands(["libmpv2=2", "xvfb=3"], changes)
        self.assertEqual(operands, ["libmpv2=1", "xvfb=3"])
        self.assertEqual(overrides, [{"package": "libmpv2", "requested": "2", "frozen": "1"}])

    def test_failed_baseline_does_not_claim_preserved_packages_or_retry(self):
        with patch.object(inventory, "run", return_value={"exit": 100, "output": "conflict"}) as run:
            result = inventory.solve_group(("libmpv2",), {"libmpv2": "2"}, lambda *_: {})
        self.assertEqual(run.call_count, 1)
        self.assertIn("baseline simulation did not succeed", result["constrained"]["blocked"])
        self.assertIsNone(result["constrained"]["preserves_installed"])

    def test_unexecuted_constrained_solve_is_blocked_and_unknown(self):
        calls = []
        def fake(args, timeout=60):
            calls.append(args)
            if args[0] == "dpkg":
                return {"exit": 0 if args[3] == "gt" else 1}
            if "shared=1" in args:
                return {"exit": None, "error": "collector_budget_exhausted"}
            return {"exit": 0, "output": "Inst shared [1] (2 repo [amd64])\n"}
        with patch.object(inventory, "run", side_effect=fake):
            result = inventory.solve_group(("libmpv2",), {"libmpv2": "2"}, lambda *_: {"output": ""})
        self.assertIn("budget exhausted", result["constrained"]["simulation"]["blocked"])
        self.assertIsNone(result["constrained"]["preserves_installed"])
        self.assertEqual(sum(args[0] == "apt-get" for args in calls), 2)

    def test_development_candidates_must_match(self):
        with patch.object(inventory, "run") as run:
            result = inventory.solve_group(inventory.GROUPS["development"],
                                           {"libmpv-dev": "2", "libmpv2": "1"}, lambda *_: {})
        self.assertIn("candidate versions differ", result["simulation"]["blocked"])
        run.assert_not_called()

    def test_mac_guard_reports_block_without_session_creation(self):
        with patch.object(inventory, "run", return_value={"exit": 0, "output": ""}), \
             patch.object(inventory.os, "getuid", return_value=501, create=True), \
             patch.object(inventory, "verify_existing_session", side_effect=RuntimeError("BLOCKED: no GUI")):
            result = inventory.mac_inventory()
        self.assertEqual(result["uid"], 501)
        self.assertEqual(result["existing_gui_session"], "BLOCKED: no GUI")

    def test_mac_metadata_queries_named_modules_without_updating(self):
        calls = []
        def query(args, timeout=15):
            calls.append(args)
            return {"exit": 1, "output": "unavailable"}
        with patch.object(inventory.shutil, "which", side_effect=lambda name: "/usr/bin/" + name), \
             patch.object(inventory, "mac_metadata_command", side_effect=query):
            report = inventory.mac_build_metadata()
        self.assertEqual(set(report["modules"]), {"libavcodec", "libavformat", "libavutil", "libswresample",
                                                   "libswscale", "libass", "libplacebo"})
        self.assertIn(["/usr/bin/pkg-config", "--modversion", "libavcodec"], calls)
        self.assertFalse(any("update" in arg or "install" in arg for call in calls for arg in call))
        self.assertEqual(report["tools"]["meson"]["path"], "/usr/bin/meson")

    def test_mac_metadata_respects_exhausted_budget(self):
        with patch.object(inventory, "DEADLINE", 10), patch.object(inventory.time, "monotonic", return_value=11), \
             patch.object(inventory.subprocess, "run") as launch:
            result = inventory.mac_metadata_command(["/usr/bin/meson", "--version"])
        self.assertEqual(result["error"], "collector_budget_exhausted")
        launch.assert_not_called()


if __name__ == "__main__":
    unittest.main()
