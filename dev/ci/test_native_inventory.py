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
    def test_configured_path_and_toolchain_are_preserved(self):
        sep = native_inventory.os.pathsep
        node = "/Users/runner/.nvm/versions/node/v24.14.0/bin"
        env = {"PATH": sep.join([node, "/usr/bin"]), "CC": "/configured/cc",
               "CXX": "/configured/cxx", "CPATH": "/configured/include",
               "LIBRARY_PATH": "/configured/lib", "CMAKE_PREFIX_PATH": "/configured/qt",
               "PKG_CONFIG_PATH": "/configured/pkgconfig"}
        original = env.copy()
        native_inventory.prepare_process_paths(env, [], ["/usr/local/bin", "/usr/bin", "/bin"])
        self.assertIn(node, env["PATH"].split(sep))
        self.assertTrue(env["PATH"].startswith(original["PATH"]))
        for name in ("CC", "CXX", "CPATH", "LIBRARY_PATH", "CMAKE_PREFIX_PATH", "PKG_CONFIG_PATH"):
            self.assertEqual(env[name], original[name])

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
