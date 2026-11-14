"""Unexecuted source tests for the reviewed-module bootstrap boundary."""

import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("producer_bootstrap", ROOT / "dev/ci/recovery_mpv_producer/bootstrap.py")
bootstrap = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bootstrap)


class BootstrapTests(unittest.TestCase):
    def test_changed_module_is_rejected_before_loading_any_module(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "one.py").write_text("raise AssertionError('must not execute')")
            manifest = {"reviewed_source_sha": "a" * 40, "files": {"one.py": "b" * 64}}
            payload = json.dumps(manifest).encode()
            (root / "review.json").write_bytes(payload)
            with patch.object(bootstrap, "MODULES", (("first_test_module", "one.py"),)):
                with self.assertRaisesRegex(ValueError, "module identity"):
                    bootstrap.load_reviewed(root, root / "review.json",
                                            hashlib.sha256(payload).hexdigest(), "a" * 40)

    def test_duplicate_manifest_keys_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "review.json"
            data = b'{"reviewed_source_sha":"x","reviewed_source_sha":"y"}'
            path.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "duplicate"):
                bootstrap.load_reviewed(directory, path, hashlib.sha256(data).hexdigest(), "a" * 40)
