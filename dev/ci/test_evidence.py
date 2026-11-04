"""Exercise job evidence lifecycle without launching a native build."""
import json
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace
from contextlib import redirect_stdout

import evidence
MPVQT_SHA = "2ebbd38bf65d01be9c55d673ecd43c8bb9a6fc8e"


class EvidenceTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ci-evidence-test-", dir=Path(__file__).resolve().parents[2])
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.environment = {"CI_JOB_NAME": "linux-static", "CI_JOB_ID": "11",
                            "CI_COMMIT_SHA": "abc", "CI_COMMIT_REF_NAME": "feature/test", "CI_PIPELINE_ID": "22"}

    def run_job(self, child):
        with patch.object(evidence, "ROOT", self.root), patch.object(evidence, "verify_source", return_value=("abc", MPVQT_SHA)), patch.dict(os.environ, self.environment, clear=True), \
                patch.object(evidence, "run_process", side_effect=lambda command, cwd, env, platform, log: child(command, cwd=cwd, env=env)):
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

    def test_source_identity_and_cleanliness(self):
        def probe(outputs):
            return patch.object(evidence.subprocess, "run", side_effect=[SimpleNamespace(stdout=value) for value in outputs])
        with probe(["abc\n", "", " 2ebbd38 external/mpvqt\n", MPVQT_SHA + "\n"]):
            self.assertEqual(evidence.verify_source(self.root, "abc"), ("abc", MPVQT_SHA))
        with probe(["wrong\n"]), self.assertRaisesRegex(ValueError, "HEAD does not match"):
            evidence.verify_source(self.root, "abc")
        with probe(["abc\n", " M source.cpp\n"]), self.assertRaisesRegex(ValueError, "dirty"):
            evidence.verify_source(self.root, "abc")
        with probe(["abc\n", "", "+2ebbd38 external/mpvqt\n"]), self.assertRaisesRegex(ValueError, "submodule"):
            evidence.verify_source(self.root, "abc")
        with probe(["abc\n", "", " 2ebbd38 external/mpvqt\n", "wrong\n"]), self.assertRaisesRegex(ValueError, "MpvQt source revision"):
            evidence.verify_source(self.root, "abc")

    def test_unix_process_uses_pipe_and_preserves_output_and_exit(self):
        process = SimpleNamespace(stdout=io.StringIO("one\ntwo\n"), wait=lambda: 9, poll=lambda: 9)
        output = io.StringIO()
        with patch.object(evidence.subprocess, "Popen", return_value=process) as popen, redirect_stdout(output):
            code = evidence.run_process(["example"], self.root, {}, "linux", self.root / "native.log")
        self.assertEqual(code, 9)
        self.assertEqual(output.getvalue(), "one\ntwo\n")
        self.assertEqual((self.root / "native.log").read_text(), "one\ntwo\n")
        self.assertEqual(popen.call_args.kwargs["stdout"], evidence.subprocess.PIPE)
        self.assertEqual(popen.call_args.kwargs["stderr"], evidence.subprocess.STDOUT)
        self.assertNotIn("creationflags", popen.call_args.kwargs)

    def test_output_failure_terminates_and_waits_for_owned_child(self):
        class BrokenOutput:
            def __iter__(self):
                raise OSError("output failed")
            def close(self):
                pass
        calls = []
        process = SimpleNamespace(stdout=BrokenOutput(), poll=lambda: None,
                                  terminate=lambda: calls.append("terminate"),
                                  wait=lambda timeout=None: calls.append(("wait", timeout)))
        with patch.object(evidence.subprocess, "Popen", return_value=process), self.assertRaisesRegex(OSError, "output failed"):
            evidence.run_process(["example"], self.root, {}, "linux", self.root / "native.log")
        self.assertEqual(calls, ["terminate", ("wait", 10)])

    @unittest.skipUnless(os.name == "nt", "Windows PowerShell integration")
    def test_hidden_powershell_streams_stdout_stderr_and_exit(self):
        output = io.StringIO()
        command = ["powershell.exe", "-NoLogo", "-NoProfile", "-NonInteractive", "-Command",
                   "Write-Host 'child-output'; [Console]::Error.WriteLine('child-error'); exit 7"]
        with redirect_stdout(output):
            code = evidence.run_process(command, self.root, os.environ.copy(), "windows", self.root / "native.log")
        self.assertEqual(code, 7)
        self.assertIn("child-output", output.getvalue())
        self.assertIn("child-error", output.getvalue())
        self.assertEqual((self.root / "native.log").read_text(), output.getvalue())


if __name__ == "__main__":
    unittest.main()
