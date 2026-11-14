"""Source-only tests for the production verifier; execution is held."""

import hashlib
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import adapter
import recipe_edits


class VerifierTests(unittest.TestCase):
    def test_verified_bytes_and_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, "component").write_bytes(b"locked")
            record = {"path": "component", "size": 6,
                      "sha256": hashlib.sha256(b"locked").hexdigest()}
            self.assertEqual(adapter.verify_file(directory, record), b"locked")
            record["size"] = 7
            with self.assertRaises(ValueError):
                adapter.verify_file(directory, record)

    def test_ambiguous_paths_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            for path in ("../outside", "/absolute", "a//b", "a\\b", "C:/file"):
                with self.subTest(path=path), self.assertRaises(ValueError):
                    adapter.checked_path(directory, path)

    def test_unknown_and_unresolved_lock_are_rejected(self):
        with self.assertRaises(ValueError):
            adapter.plan(".", ".", {"provider": adapter.PROVIDER,
                         "rust": "nightly-2026-10-11", "components": {}}, ".")
        with self.assertRaises(ValueError):
            adapter.plan(".", ".", {"provider": adapter.PROVIDER,
                         "rust": "nightly-2026-10-11", "components": {}, "extra": 1}, ".")

    def test_recipe_edit_checks_identity_and_exactly_one_site(self):
        path = "packages/mpv.cmake"
        source = b"CONFIGURE_COMMAND meson setup <BINARY_DIR> <SOURCE_DIR>\n"
        digest = hashlib.sha256(source).hexdigest()
        with mock.patch.dict(recipe_edits.MESON, {path: digest}):
            edited = recipe_edits.edit(path, source)
            self.assertIn(b"meson setup --wrap-mode=nodownload", edited)
            with self.assertRaises(ValueError):
                recipe_edits.edit(path, source + source)
        with self.assertRaises(ValueError):
            recipe_edits.edit("packages/unknown.cmake", source)


if __name__ == "__main__":
    unittest.main()
