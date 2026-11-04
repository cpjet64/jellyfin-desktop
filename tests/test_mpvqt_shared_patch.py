"""Check the narrow shared-MpvQt compatibility patch on disposable copies."""

import importlib.util
import os
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(os.environ.get("MPVQT_TEST_SOURCE", ROOT / "external/mpvqt"))
SPEC = importlib.util.spec_from_file_location("shared_patch", ROOT / "dev/ci/patch_mpvqt_shared.py")
PATCH = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH)


class SharedPatchTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="mpvqt-shared-patch-", dir=ROOT)
        self.addCleanup(self.temp.cleanup)
        self.copy = Path(self.temp.name)
        (self.copy / "src").mkdir()
        for name in PATCH.FILES:
            shutil.copyfile(SOURCE / name, self.copy / name)

    def test_pristine_copy_and_double_application(self):
        source_hashes = {name: PATCH.sha((SOURCE / name).read_bytes()) for name in PATCH.FILES}
        PATCH.apply(self.copy)
        for name, (_, expected) in PATCH.FILES.items():
            self.assertEqual(PATCH.sha((self.copy / name).read_bytes()), expected)
            self.assertEqual(PATCH.sha((SOURCE / name).read_bytes()), source_hashes[name])
        with self.assertRaises(ValueError):
            PATCH.apply(self.copy)

    def test_tampered_input_rejected_without_partial_changes(self):
        header = self.copy / "src/mpvcontroller.h"
        implementation = self.copy / "src/mpvcontroller.cpp"
        header.write_bytes(header.read_bytes() + b"tamper")
        before = implementation.read_bytes()
        with self.assertRaises(ValueError):
            PATCH.apply(self.copy)
        self.assertEqual(implementation.read_bytes(), before)

    def test_lf_input_supported(self):
        for name in PATCH.FILES:
            path = self.copy / name
            path.write_bytes(path.read_bytes().replace(b"\r\n", b"\n"))
        PATCH.apply(self.copy)
        self.assertEqual(PATCH.sha((self.copy / "src/mpvcontroller.h").read_bytes()),
                         PATCH.FILES["src/mpvcontroller.h"][1])

    def test_checkout_is_rejected(self):
        (self.copy / ".git").write_text("gitdir: ignored")
        with self.assertRaises(ValueError):
            PATCH.apply(self.copy)


if __name__ == "__main__":
    unittest.main()
