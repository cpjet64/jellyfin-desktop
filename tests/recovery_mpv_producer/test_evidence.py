"""Unexecuted evidence-binding decision tests."""

import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("producer_evidence", ROOT / "dev/ci/recovery_mpv_producer/evidence.py")
evidence = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(evidence)


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        proof = b"reviewed evidence fixture"
        (self.root / "proof.txt").write_bytes(proof)
        proof_sha = hashlib.sha256(proof).hexdigest()
        self.lock = {"provider_commit": "p", "mpv_commit": "m", "sources": [],
                     "provider_adapter": {"overlays": ["x"]}, "cargo_inventories": {"subrandr": "c"},
                     "rust_manifest_sha256": "r", "rust_components": {"rustc": "r"},
                     "host_tools": {"git": "g"}}
        self.review = {"reviewed_source_sha": "a" * 40, "files": {"proof.txt": proof_sha}}
        subjects = evidence.expected_bindings(self.lock, "b" * 64, "c" * 64, self.review)
        self.records = [{"kind": kind, "status": "qualified", "subject_sha256": subjects[kind],
                         "proof_path": "proof.txt", "proof_sha256": proof_sha} for kind in evidence.KINDS]

    def validate(self):
        evidence.validate_evidence(self.root, self.lock, "b" * 64, "c" * 64,
                                   self.review, {"schema_version": 1, "records": self.records})

    def test_requires_complete_and_qualified_records(self):
        self.validate()
        self.records.pop()
        with self.assertRaisesRegex(ValueError, "missing"):
            self.validate()

    def test_changed_subject_and_proof_are_rejected(self):
        self.lock["provider_adapter"]["overlays"].append("changed")
        with self.assertRaisesRegex(ValueError, "subject"):
            self.validate()
        self.lock["provider_adapter"]["overlays"].pop()
        (self.root / "proof.txt").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "proof"):
            self.validate()

    def test_boolean_is_not_qualification(self):
        self.records[0]["status"] = True
        with self.assertRaisesRegex(ValueError, "unqualified"):
            self.validate()
