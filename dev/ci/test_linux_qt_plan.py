from dataclasses import replace
import io
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import Mock, patch

import linux_qt_archive
import linux_qt_library
from linux_qt_plan import MAX_FILE, Replay, attach_replays, scan_archives
from qt_sdk_archive import Entry, stage_entries


ROOT = "6.9.3/gcc_64"


class FakeReader:
    def __init__(self, entries, data=b"abc"):
        self.entries = iter(entries)
        self.data = io.BytesIO(data)
        self.read_sizes = []

    def next_entry(self):
        return next(self.entries, None)

    def read(self, size):
        self.read_sizes.append(size)
        return self.data.read(size)


class PlanTest(unittest.TestCase):
    def test_stage_rejects_non_linux_without_starting_worker(self):
        with patch("linux_qt_archive.sys.platform", "win32"), \
                patch("linux_qt_archive.run_worker") as worker:
            with self.assertRaises(ValueError):
                linux_qt_archive.stage_qt("unused")
            worker.assert_not_called()

    def test_loader_rejects_non_linux_before_host_verification(self):
        with patch("linux_qt_library.sys.platform", "win32"), \
                patch("linux_qt_library.verify_library") as verify:
            with self.assertRaises(ValueError):
                linux_qt_library.load_library()
            verify.assert_not_called()

    def test_settle_waits_for_descendants_after_root_exit(self):
        child = Mock(pid=123)
        with patch("linux_qt_archive.os.killpg", create=True,
                   side_effect=[None, None, ProcessLookupError]) as signal_group, \
                patch("linux_qt_archive.signal.SIGKILL", 9, create=True), \
                patch("linux_qt_archive.time.sleep") as sleep:
            linux_qt_archive.settle_group(child)
        self.assertEqual([call.args for call in signal_group.call_args_list],
                         [(123, 9), (123, 0), (123, 0)])
        child.wait.assert_called_once_with(timeout=linux_qt_archive.GROUP_EXIT_SECONDS)
        sleep.assert_called_once()

    def test_settle_fails_when_group_remains(self):
        child = Mock(pid=123)
        with patch("linux_qt_archive.os.killpg", create=True), \
                patch("linux_qt_archive.signal.SIGKILL", 9, create=True), \
                patch("linux_qt_archive.time.monotonic", side_effect=[0, 6]), \
                patch("linux_qt_archive.time.sleep"):
            with self.assertRaisesRegex(RuntimeError, "did not disappear"):
                linux_qt_archive.settle_group(child)

    def test_primary_worker_failure_survives_cleanup_failure(self):
        child = Mock(pid=123)
        child.wait.return_value = 19
        with patch("linux_qt_archive.subprocess.Popen", return_value=child), \
                patch("linux_qt_archive.settle_group", side_effect=RuntimeError("group remains")):
            with self.assertRaisesRegex(RuntimeError, "worker exit 19") as failure:
                linux_qt_archive.run_worker(Path("."))
        self.assertIn("group remains", failure.exception.__notes__[0])
        self.assertIn("address_space_bytes", str(failure.exception))

    def test_replay_detects_changed_header_and_new_entry(self):
        expected = [Entry("x", "file", 3, 0o644)]
        changed = Replay(FakeReader([replace(expected[0], mode=0o755)]), expected)
        with self.assertRaises(ValueError):
            with changed.open_file(0):
                pass
        extra = Replay(FakeReader([expected[0], Entry("y", "directory")]), expected)
        with extra.open_file(0):
            pass
        with self.assertRaises(ValueError):
            extra.finish()

    def test_scan_rejects_oversized_declared_file_before_staging(self):
        class ContextReader(FakeReader):
            def __enter__(self):
                return self

            def __exit__(self, *args):
                return False

        reader = ContextReader([Entry("huge", "file", MAX_FILE + 1, 0o644)])
        with patch("linux_qt_plan.Reader", return_value=reader):
            with self.assertRaises(ValueError):
                scan_archives([({"extract_subdirectory": ROOT}, "unused")], object(), ROOT)

    def test_decode_reads_through_eof_before_publication(self):
        entry = Entry("x", "file", 3, 0o644)
        reader = FakeReader([entry])
        groups, replays = attach_replays([(ROOT, [entry])], [reader])
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            result = stage_entries(groups, root / "stage", root / "ready", 100, ROOT,
                                   lambda stage: replays[0].finish())
            self.assertEqual((result / ROOT / "x").read_bytes(), b"abc")
            self.assertGreaterEqual(len(reader.read_sizes), 2)
            self.assertTrue(all(size > 0 for size in reader.read_sizes))

    def test_probe_failure_never_publishes(self):
        entry = Entry("x", "file", 3, 0o644)
        groups, replays = attach_replays([(ROOT, [entry])], [FakeReader([entry])])
        def reject(stage):
            replays[0].finish()
            raise ValueError("probe failure")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaises(ValueError):
                stage_entries(groups, root / "stage", root / "ready", 100, ROOT, reject)
            self.assertFalse((root / "ready").exists())

    def test_candidate_receipt_requires_probe_on_stable_prefix(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            prefix = root / "qt-candidate" / ROOT
            prefix.mkdir(parents=True)
            candidate = linux_qt_archive.Candidate(root, prefix, linux_qt_archive.manifest_hash())
            environment = {"PKG_CONFIG_PATH": str(prefix / "lib/pkgconfig"),
                           "LD_LIBRARY_PATH": str(prefix / "lib")}
            with self.assertRaises(ValueError):
                linux_qt_archive.complete_qt(candidate, {"environment": {}})
            self.assertFalse((root / "qt-sdk-ready.json").exists())
            def fake_link(source, target, **kwargs):
                if target.exists():
                    raise FileExistsError(target)
                shutil.copyfile(source, target)
            with patch("linux_qt_archive.os.link", side_effect=fake_link):
                self.assertEqual(linux_qt_archive.complete_qt(candidate, {"environment": environment}), prefix)
                self.assertTrue((root / "qt-sdk-ready.json").is_file())
                receipt = json.loads((root / "qt-sdk-ready.json").read_text())
                self.assertEqual(receipt["decoder_limits"], linux_qt_archive.decoder_limits())
                self.assertTrue(prefix.is_dir())
                with self.assertRaises(FileExistsError):
                    linux_qt_archive.complete_qt(candidate, {"environment": environment})


if __name__ == "__main__":
    unittest.main()
