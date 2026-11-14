"""Unexecuted source tests for exact replacement and mandatory verification."""

import hashlib
import unittest
from unittest import mock

import staged_source


class StagedSourceTests(unittest.TestCase):
    def test_exact_mpv_site_only(self):
        source = b"ExternalProject_Add(mpv\n" + staged_source.MPV_FETCH + b")\n"
        digest = hashlib.sha256(source).hexdigest()
        with mock.patch.object(staged_source, "MPV_RECIPE", digest):
            result = staged_source.mpv_recipe(source)
            self.assertIn(staged_source.MPV_STAGED, result)
            self.assertNotIn(b"GIT_REPOSITORY", result)
            with self.assertRaises(ValueError):
                staged_source.mpv_recipe(source + b"changed")

    def test_update_helper_is_removed_without_touching_other_helpers(self):
        source = (b"prefix\n" + staged_source.START +
                  b"git fetch --filter=tree:0\nendfunction()\n\n" +
                  staged_source.END + b"endfunction()\n")
        with mock.patch.object(staged_source, "CUSTOM_STEPS", hashlib.sha256(source).hexdigest()):
            result = staged_source.custom_steps(source)
            self.assertNotIn(b"git fetch", result)
            self.assertIn(staged_source.END, result)

    def test_binding_requires_both_verifiers_and_patch(self):
        row = {"name": "mpv", "kind": "git", "commit": staged_source.MPV_COMMIT}
        with self.assertRaises(ValueError):
            staged_source.require_mpv_binding(row, [], lambda *_: None, "x", lambda *_: None)
        source_check = mock.Mock()
        patch_check = mock.Mock()
        staged_source.require_mpv_binding(row, [{"name": "bridge"}], source_check, "x", patch_check)
        source_check.assert_called_once_with(row, "x")
        patch_check.assert_called_once_with(row, "x", [{"name": "bridge"}])
