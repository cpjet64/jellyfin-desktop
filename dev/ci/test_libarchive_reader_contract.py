"""Independent archive policy and structured-reader contract checks."""
import io
import ctypes
from pathlib import Path
import stat
import tempfile
import unittest
from unittest.mock import MagicMock, patch

from qt_sdk_archive import Entry, plan_entries, stage_entries
import linux_qt_reader as reader
import linux_qt_library as library
import linux_qt_plan as plan

ROOT = "6.9.3/gcc_64"


def regular(path, data=b"x", declared=None, mode=0o644):
    return Entry(path, "file", len(data) if declared is None else declared,
                 mode, open_data=lambda: io.BytesIO(data))


def link(path, target):
    return Entry(path, "symlink", mode=0o777, link=target)


class ArchivePolicyContract(unittest.TestCase):
    def test_rejects_traversal_link_parents_and_escape_graphs(self):
        invalid = [
            [regular("../escape")],
            [link("parent", "target"), regular("parent/child")],
            [link("parent", "../../../outside")],
            [link("first", "second"), link("second", "first")],
            [link("a", "."), link("b", "a/../outside")],
        ]
        for entries in invalid:
            with self.subTest(entries=entries), self.assertRaises(ValueError):
                plan_entries([(ROOT, entries)], 1024, ROOT)

    def test_valid_relative_alias_graph_is_accepted(self):
        entries = [regular("lib/real"), link("lib/soname", "real"),
                   link("lib/name", "soname")]
        self.assertEqual(len(plan_entries([(ROOT, entries)], 1024, ROOT)), 3)

    def test_conflicting_overlay_and_data_lengths_never_publish(self):
        scenarios = [
            [regular("x", mode=0o644), regular("x", mode=0o755)],
            [Entry("d", "directory", mode=0o755), Entry("d", "directory", mode=0o700)],
            [regular("x", b"a", declared=2)],
            [regular("x", b"ab", declared=1)],
        ]
        for entries in scenarios:
            with self.subTest(entries=entries), tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp:
                root = Path(tmp)
                with self.assertRaises(ValueError):
                    stage_entries([(ROOT, entries)], root / "stage", root / "published", 1024, ROOT)
                self.assertFalse((root / "published").exists())

    def test_checksum_error_after_declared_bytes_never_publishes(self):
        class LateFailure(io.BytesIO):
            def read(self, size=-1):
                result = super().read(size)
                if not result:
                    raise ValueError("final checksum failure")
                return result
        entry = Entry("x", "file", 1, 0o644, open_data=lambda: LateFailure(b"x"))
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[2]) as tmp:
            root = Path(tmp)
            with self.assertRaisesRegex(ValueError, "checksum"):
                stage_entries([(ROOT, [entry])], root / "stage", root / "published", 1024, ROOT)
            self.assertFalse((root / "published").exists())


class StructuredReaderContract(unittest.TestCase):
    def backend(self):
        fake = MagicMock()
        fake.archive_read_new.return_value = 1
        fake.archive_read_support_filter_none.return_value = 0
        fake.archive_read_support_format_7zip.return_value = 0
        fake.archive_read_open_fd.return_value = 0
        fake.archive_read_free.return_value = 0
        fake.archive_read_data_skip.return_value = 0
        fake.archive_read_has_encrypted_entries.return_value = 0
        fake.archive_format.return_value = 0xE0000
        fake.archive_filter_code.return_value = 0
        fake.archive_entry_pathname.return_value = b"first"
        fake.archive_entry_hardlink.return_value = None
        fake.archive_entry_is_encrypted.return_value = 0
        fake.archive_entry_acl_types.return_value = 0
        fake.archive_entry_xattr_count.return_value = 0
        fake.archive_entry_sparse_count.return_value = 0
        fake.archive_entry_fflags_text.return_value = None
        fake.archive_entry_mode.return_value = stat.S_IFREG | 0o644
        fake.archive_entry_size_is_set.return_value = 1
        fake.archive_entry_size.return_value = 1
        fake.archive_entry_symlink.return_value = None
        return fake

    @staticmethod
    def header(fake, statuses):
        entry = library.ArchiveEntry()
        pointer = ctypes.pointer(entry)
        def next_header(_archive, out):
            status = statuses.pop(0)
            if status == 0:
                ctypes.cast(out, ctypes.POINTER(library.E))[0] = pointer
            return status
        fake.archive_read_next_header.side_effect = next_header

    def test_header_statuses_and_completed_encryption(self):
        for status in (-10, -20, -25, -30):
            fake = self.backend()
            self.header(fake, [status])
            with self.subTest(status=status), patch.object(reader.os, "open", return_value=7), \
                 patch.object(reader.os, "close"):
                with reader.Reader("archive", fake) as stream:
                    with self.assertRaises(ValueError):
                        stream.next_entry()
        for encrypted in (-1, 1):
            fake = self.backend()
            fake.archive_read_has_encrypted_entries.return_value = encrypted
            self.header(fake, [1])
            with self.subTest(encrypted=encrypted), patch.object(reader.os, "open", return_value=7), \
                 patch.object(reader.os, "close"):
                with reader.Reader("archive", fake) as stream:
                    with self.assertRaises(ValueError):
                        stream.next_entry()

    def test_data_one_is_one_byte_zero_is_eof_and_negative_fails(self):
        fake = self.backend()
        self.header(fake, [0])
        def read_data(_archive, buffer, _size):
            ctypes.memmove(buffer, b"Z", 1)
            return 1
        fake.archive_read_data.side_effect = read_data
        with patch.object(reader.os, "open", return_value=7), patch.object(reader.os, "close"):
            with reader.Reader("archive", fake) as stream:
                stream.next_entry()
                self.assertEqual(stream.read(8), b"Z")
                fake.archive_read_data.side_effect = [0, -30]
                self.assertEqual(stream.read(8), b"")
                with self.assertRaises(ValueError):
                    stream.read(8)

    def test_primary_error_survives_cleanup_and_fd_closed_once(self):
        fake = self.backend()
        fake.archive_read_free.return_value = -30
        with patch.object(reader.os, "open", return_value=7), patch.object(reader.os, "close") as close:
            with self.assertRaisesRegex(RuntimeError, "primary"):
                with reader.Reader("archive", fake):
                    raise RuntimeError("primary")
        fake.archive_read_free.assert_called_once()
        close.assert_called_once_with(7)

    def test_header_metadata_copied_and_encrypted_entry_rejected(self):
        fake = self.backend()
        self.header(fake, [0, 0])
        fake.archive_entry_pathname.side_effect = [b"first", b"second"]
        with patch.object(reader.os, "open", return_value=7), patch.object(reader.os, "close"):
            with reader.Reader("archive", fake) as stream:
                first = stream.next_entry()
                second = stream.next_entry()
                self.assertEqual((first.path, second.path), ("first", "second"))
        fake = self.backend()
        self.header(fake, [0])
        fake.archive_entry_is_encrypted.return_value = 1
        with patch.object(reader.os, "open", return_value=7), patch.object(reader.os, "close"):
            with reader.Reader("archive", fake) as stream:
                with self.assertRaises(ValueError):
                    stream.next_entry()

    def test_privileged_file_flags_are_rejected(self):
        fake = self.backend()
        self.header(fake, [0])
        fake.archive_entry_fflags_text.return_value = b"uchg"
        with patch.object(reader.os, "open", return_value=7), patch.object(reader.os, "close"):
            with reader.Reader("archive", fake) as stream:
                with self.assertRaises(ValueError):
                    stream.next_entry()

    def test_abi_declares_every_reader_symbol(self):
        required = {"archive_version_number", "archive_read_new", "archive_read_support_filter_none",
                    "archive_read_support_format_7zip", "archive_read_open_fd", "archive_read_next_header",
                    "archive_read_data", "archive_read_data_skip", "archive_read_has_encrypted_entries",
                    "archive_format", "archive_filter_code", "archive_read_free", "archive_entry_pathname",
                    "archive_entry_hardlink", "archive_entry_symlink", "archive_entry_size",
                    "archive_entry_size_is_set", "archive_entry_mode", "archive_entry_is_encrypted",
                    "archive_entry_acl_types", "archive_entry_xattr_count", "archive_entry_sparse_count",
                    "archive_entry_fflags_text"}
        self.assertEqual(set(library.ABI), required)
        for name, (result, arguments) in library.ABI.items():
            with self.subTest(name=name):
                self.assertIsNotNone(result)
                self.assertIsInstance(arguments, list)


class ReplayContract(unittest.TestCase):
    def test_changed_or_extra_headers_are_rejected(self):
        first = Entry("one", "file", 1)
        changed = Entry("one", "file", 2)
        fake = MagicMock()
        fake.next_entry.return_value = changed
        replay = plan.Replay(fake, [first])
        with self.assertRaisesRegex(ValueError, "metadata changed"):
            with replay.open_file(0):
                pass
        fake.next_entry.side_effect = [first, Entry("extra", "directory")]
        replay = plan.Replay(fake, [first])
        with replay.open_file(0):
            pass
        with self.assertRaisesRegex(ValueError, "gained"):
            replay.finish()

    def test_replay_refuses_skipping_payload(self):
        first = Entry("one", "file", 1)
        second = Entry("two", "file", 1)
        fake = MagicMock()
        fake.next_entry.side_effect = [first, second]
        replay = plan.Replay(fake, [first, second])
        with self.assertRaisesRegex(ValueError, "skipped"):
            with replay.open_file(1):
                pass


if __name__ == "__main__":
    unittest.main()
