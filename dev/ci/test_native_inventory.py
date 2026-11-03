"""Verify inventory remains complete when all optional tools are absent."""

from contextlib import redirect_stdout
import io
from pathlib import Path
import sys
import unittest
from types import SimpleNamespace
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

    def test_linux_package_inventory_includes_build_and_display_inputs(self):
        calls = []
        def run(args, **kwargs):
            calls.append(args)
            return SimpleNamespace(returncode=0, stdout="Installed: (none)\nCandidate: 1.2.3\n")
        with redirect_stdout(io.StringIO()), patch.object(native_inventory.shutil, "which", return_value="/usr/bin/tool"), \
                patch.object(native_inventory, "probe", return_value="unavailable"), \
                patch.object(native_inventory.subprocess, "run", side_effect=run):
            native_inventory.package_versions()
        checked = {args[-1] for args in calls}
        self.assertTrue({"xvfb", "mesa-utils", "qt6-base-dev", "qt6-webengine-dev", "libmpv-dev"}.issubset(checked))
        self.assertTrue(all(args[1] == "policy" for args in calls))

    def test_macos_brew_queries_do_not_install_or_update(self):
        calls = []
        def run(args, **kwargs):
            calls.append((args, kwargs["env"]))
            payload = '{"formulae":[{"name":"mpv","installed":[{"version":"1.0"}]}]}'
            return SimpleNamespace(returncode=0, stdout=payload if "info" in args else "mpv 1.0")
        with redirect_stdout(io.StringIO()), patch.object(native_inventory.shutil, "which", return_value="/usr/local/bin/brew"), \
                patch.object(native_inventory.subprocess, "run", side_effect=run):
            native_inventory.brew_versions()
        self.assertEqual([args[1] for args, _ in calls], ["list", "info"])
        self.assertTrue(all(env["HOMEBREW_NO_AUTO_UPDATE"] == "1" for _, env in calls))


if __name__ == "__main__":
    unittest.main()
