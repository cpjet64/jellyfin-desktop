"""Source-only contract tests; execution requires separate authorization."""

import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import recovery_artifacts as artifacts
import recovery_lock_preflight as preflight


class RecoveryArtifactContracts(unittest.TestCase):
    def test_duplicate_manifest_keys_are_rejected(self):
        with self.assertRaises(ValueError):
            preflight.parse_object(b'{"status":"locked","status":"unresolved"}')

    def test_lock_digest_mismatch_fails_before_trusting_status(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "lock.json"
            path.write_text('{"status":"locked"}', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "digest"):
                preflight.verify(path, "0" * 64)

    def test_incomplete_lock_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "not complete"):
            preflight.check_configuration({"schema_version": 1, "status": "unresolved"})

    def test_floating_git_source_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "commit not pinned"):
            preflight.check_inputs({"sources": [{"name": "example", "kind": "git",
                                                  "url": "https://example.invalid/repo",
                                                  "commit": "main"}]})

    def test_traversal_and_absolute_paths_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            for path in ("../outside.dll", "/outside.dll", "dir/../outside.dll", "C:/outside.dll"):
                with self.subTest(path=path), self.assertRaises(ValueError):
                    artifacts.contained_file(Path(directory), path)

    def test_duplicate_artifact_paths_are_rejected(self):
        config = {"dll": "same", "msvc_import_library": "same", "headers": ["header"],
                  "dependency_closure": []}
        with self.assertRaisesRegex(ValueError, "duplicate"):
            artifacts.checked_paths(config)

    def test_required_variant_cannot_reuse_other_variant_paths(self):
        lock = {"variants": {"x86_64": {"dll": "mpv/libmpv-2.dll",
                                           "msvc_import_library": "mpv/libmpv-2.dll.lib",
                                           "headers": ["mpv/include/mpv/client.h"],
                                           "dependency_closure": []}}}
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "unexpected installed"):
                artifacts.variant_manifest(Path(directory), "x86_64", lock)

    def test_dumpbin_symbol_parsers_distinguish_exports_and_imports(self):
        exports = "    1    0 00001000 mpv_render_context_render\n"
        members = "  1A2B __imp_mpv_render_context_render\n  1A3B mpv_command\n"
        self.assertEqual(set(artifacts.EXPORT.findall(exports)), {"mpv_render_context_render"})
        self.assertEqual(set(artifacts.IMPORT.findall(members)), {"mpv_render_context_render", "mpv_command"})

    def test_missing_private_export_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "mpv.dll").write_bytes(b"dll")
            (root / "mpv.lib").write_bytes(b"library")
            config = {"dll": "mpv.dll", "msvc_import_library": "mpv.lib"}
            lock = {"required_public_exports": ["mpv_render_context_render"],
                    "required_private_exports": ["mpv_render_context_render_recovery_v1"]}
            outputs = ["8664 machine (x64)", "8664 machine (x64)",
                       "  1 0 00001000 mpv_render_context_render\n",
                       "  1A __imp_mpv_render_context_render\n"]
            with mock.patch.object(artifacts, "dumpbin", side_effect=outputs):
                with self.assertRaisesRegex(ValueError, "missing"):
                    artifacts.verify_binary(root, Path("dumpbin"), config, lock)

    def test_packaged_fallback_must_match_approved_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "libmpv-2.dll").write_bytes(b"primary")
            (root / "libmpv-fallback.dll").write_bytes(b"stale")
            primary = artifacts.artifact(root, "libmpv-2.dll")
            expected = {"sha256": "0" * 64, "size": 5}
            manifest = {"variants": {"x86_64_v3": {"dll": primary},
                                     "x86_64": {"dll": expected}}}
            with self.assertRaisesRegex(ValueError, "fallback"):
                artifacts.verify_packaged(root, manifest)

    def test_incomplete_lock_prevents_manifest_production(self):
        args = ["recovery_artifacts.py", "produce", "--lock", "missing.json",
                "--approved-lock-sha256", "0" * 64, "--root", ".", "--qt-root", ".",
                "--manifest", "never-created.json"]
        with mock.patch.object(sys, "argv", args), mock.patch.object(artifacts, "build_manifest") as build:
            self.assertEqual(artifacts.main(), 1)
            build.assert_not_called()


if __name__ == "__main__":
    unittest.main()
