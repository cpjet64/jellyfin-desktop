"""Verify inventory remains complete when all optional tools are absent."""

from contextlib import redirect_stdout
import io
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
import native_inventory


class TestInventory(unittest.TestCase):
    def test_minimum_tool_versions(self):
        self.assertTrue(native_inventory.version_satisfies("cmake version 4.1.2", (3, 21)))
        self.assertFalse(native_inventory.version_satisfies("cmake version 3.20.9", (3, 21)))
        self.assertTrue(native_inventory.version_satisfies("v22.21.1", (20, 0)))
        self.assertFalse(native_inventory.version_satisfies("version unknown", (20, 0)))

    def test_missing_tools_do_not_short_circuit_inventory(self):
        output = io.StringIO()
        with patch.object(native_inventory.shutil, "which", return_value=None), \
                patch.object(Path, "is_file", return_value=False), \
                patch.dict(native_inventory.os.environ, {}, clear=True), redirect_stdout(output):
            found = native_inventory.inventory("Linux", "static")
        text = output.getvalue()
        for expected in ("Tool cmake: MISSING", "Tool glxinfo: MISSING", "Module mpv: unavailable",
                         "Module Qt6WebEngineQuick: unavailable", "Prerequisite MpvQtConfig.cmake:"):
            self.assertIn(expected, text)
        self.assertTrue(all(value is None for value in found.values()))


if __name__ == "__main__":
    unittest.main()
