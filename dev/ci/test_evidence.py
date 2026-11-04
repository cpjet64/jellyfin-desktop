"""Exercise job evidence lifecycle without launching a native build."""
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import evidence


class EvidenceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2] / "deps")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.environment = {"CI_JOB_NAME": "linux-static", "CI_JOB_ID": "11",
                            "CI_COMMIT_SHA": "abc", "CI_COMMIT_REF_NAME": "feature/test"}

    def run_job(self, child):
        with patch.object(evidence, "ROOT", self.root), patch.dict(os.environ, self.environment, clear=True), \
                patch.object(evidence.subprocess, "call", side_effect=child):
            code = evidence.run_job()
        path = self.root / "build/ci-evidence/linux-static/evidence.json"
        return code, json.loads(path.read_text(encoding="utf-8"))

    def test_preflight_failure_is_recorded(self):
        code, report = self.run_job(lambda *args, **kwargs: 2)
        self.assertEqual(code, 2)
        self.assertEqual((report["build_core"], report["offscreen"], report["desktop"]),
                         ("failed", "not_run", "blocked"))
        self.assertEqual((report["sha"], report["ref"], report["job_id"]), ("abc", "feature/test", "11"))

    def test_success_preserves_explicit_desktop_block(self):
        def child(args, cwd, env):
            directory = Path(env["CI_EVIDENCE_DIR"])
            evidence.update(directory, "build_core", "passed", "core passed")
            evidence.update(directory, "offscreen", "passed", "three scenarios passed")
            return 0
        code, report = self.run_job(child)
        self.assertEqual(code, 0)
        self.assertEqual((report["build_core"], report["offscreen"], report["desktop"]),
                         ("passed", "passed", "blocked"))

    def test_blocked_render_remains_distinct_from_failure(self):
        def child(args, cwd, env):
            directory = Path(env["CI_EVIDENCE_DIR"])
            evidence.update(directory, "build_core", "passed", "core passed")
            evidence.update(directory, "offscreen", "blocked", "Xvfb absent")
            return 1
        code, report = self.run_job(child)
        self.assertEqual((code, report["build_core"], report["offscreen"]), (1, "passed", "blocked"))
        self.assertEqual(report["offscreen_reason"], "Xvfb absent")


if __name__ == "__main__":
    unittest.main()
