import unittest
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
        items = inventory.transaction(text)
        self.assertEqual([item["action"] for item in items], ["upgrade", "install", "remove"])
        self.assertEqual(items[0]["version"], "0.35")
        self.assertEqual(inventory.package_sizes("Size: 42\nInstalled-Size: 12\n"),
                         {"Size": 42, "Installed-Size": 12288})

    def test_command_allowlist_rejects_mutating_forms(self):
        for command in (["apt-get", "install", "xvfb"], ["apt-get", "update"],
                        ["apt-cache", "policy", "--option"], ["apt-get", "--simulate",
                         "--no-install-recommends", "install", "xvfb=1", "-o"],
                        ["brew", "update"], ["xcrun", "--run", "sh"],
                        ["stat", "-f", "%u", "/etc/passwd"]):
            with self.subTest(command=command), self.assertRaises(ValueError):
                inventory.run(command)

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
        self.assertIn(["apt-get", "--simulate", "--no-install-recommends", "install",
                       *[name + "=1.2-3" for name in inventory.PACKAGES]], calls)
        self.assertIsNone(result["download_bytes_sum"])
        self.assertIsNone(result["installed_bytes_sum"])

    def test_missing_candidate_never_simulates(self):
        with patch.object(inventory, "run", return_value={"exit": 0, "output": "Candidate: (none)"}) as call, \
             patch.object(inventory, "index_metadata", return_value={}):
            result = inventory.linux_inventory()
        self.assertIn("blocked", result["simulation"])
        self.assertFalse(any(args.args[0][0] == "apt-get" for args in call.call_args_list))

    def test_mac_guard_reports_block_without_session_creation(self):
        with patch.object(inventory, "run", return_value={"exit": 0, "output": ""}), \
             patch.object(inventory.os, "getuid", return_value=501, create=True), \
             patch.object(inventory, "verify_existing_session", side_effect=RuntimeError("BLOCKED: no GUI")):
            result = inventory.mac_inventory()
        self.assertEqual(result["uid"], 501)
        self.assertEqual(result["existing_gui_session"], "BLOCKED: no GUI")


if __name__ == "__main__":
    unittest.main()
