"""Explicitly gated native fixture smoke checks; job qualification is separate."""
import os
from pathlib import Path
import subprocess
import sys
import unittest


ENABLED = sys.platform == "win32" and os.environ.get("BUILD_ONLY_NATIVE_QUALIFY") == "1"
CHILD = Path(__file__).resolve().parent / "build_only" / "fixture_child.py"


@unittest.skipUnless(ENABLED, "explicit Windows qualification flag required")
class NativeFixtureSmokeTests(unittest.TestCase):
    def test_finite_root_modes(self):
        for mode, expected in (("natural", 0), ("rootfail", 7), ("transientchild", 0),
                               ("spawnlate", 0)):
            with self.subTest(mode=mode):
                result = subprocess.run([sys.executable, str(CHILD), mode], timeout=10,
                                        creationflags=subprocess.CREATE_NO_WINDOW,
                                        capture_output=True, check=False)
                self.assertEqual(result.returncode, expected)
