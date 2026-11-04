import copy
import gzip
import hashlib
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

import macos_sdist_select as subject


def setUpModule():
    (subject.bounded.REPO / "deps").mkdir(exist_ok=True)


class SelectionTests(unittest.TestCase):
    def create_fixtures(self, directory, mutation=None):
        specs = copy.deepcopy(subject.ARCHIVES)
        for archive_name, spec in specs.items():
            path = directory / archive_name
            members = [(spec["root"] + "/" + member, b"fixture text\n", tarfile.REGTYPE)
                       for member in spec["members"]]
            if mutation:
                members = mutation(archive_name, members)
            with tarfile.open(path, "w:gz", format=tarfile.USTAR_FORMAT) as archive:
                for name, data, kind in members:
                    entry = tarfile.TarInfo(name)
                    entry.type = kind
                    entry.size = len(data) if kind == tarfile.REGTYPE else 0
                    archive.addfile(entry, io.BytesIO(data) if entry.size else None)
            raw = path.read_bytes()
            spec["size"] = len(raw)
            spec["sha256"] = hashlib.sha256(raw).hexdigest()
        return specs

    def test_selects_exact_nine_without_acquisition(self):
        with tempfile.TemporaryDirectory(dir=subject.bounded.REPO / "deps") as temp:
            directory = Path(temp)
            specs = self.create_fixtures(directory)
            with patch.object(subject, "ARCHIVES", specs), patch.object(subject.bounded, "acquire") as acquire:
                result = subject.select(subject.validate_directory(directory))
            acquire.assert_not_called()
            self.assertEqual(len(result["members"]), 9)
            self.assertEqual({r["archive"] for r in result["members"]}, set(specs))
            self.assertEqual({r["member"] for r in result["members"]},
                             {s["root"] + "/" + m for s in specs.values() for m in s["members"]})
            for record in result["members"]:
                self.assertEqual(record["archive_bytes"], specs[record["archive"]]["size"])
                self.assertEqual(record["bytes"], len(b"fixture text\n"))
                self.assertEqual(record["sha256"], hashlib.sha256(b"fixture text\n").hexdigest())
            self.assertEqual({p.name for p in directory.iterdir()}, set(specs))

    def test_all_hashes_checked_before_member_read(self):
        with tempfile.TemporaryDirectory(dir=subject.bounded.REPO / "deps") as temp:
            directory = Path(temp)
            specs = self.create_fixtures(directory)
            last = list(specs)[-1]
            (directory / last).write_bytes(b"wrong")
            with patch.object(subject, "ARCHIVES", specs), patch.object(subject.bounded, "inspect_stream") as inspect:
                with self.assertRaises(ValueError):
                    subject.select(directory)
            inspect.assert_not_called()

    def test_missing_duplicate_and_nonregular_fail(self):
        def mutate(mode):
            def apply(name, members):
                if name != "cython-3.3.1.tar.gz":
                    return members
                if mode == "missing":
                    return members[1:]
                if mode == "duplicate":
                    return members + [members[0]]
                return [(members[0][0], b"", tarfile.SYMTYPE)] + members[1:]
            return apply
        for mode in ("missing", "duplicate", "link"):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory(dir=subject.bounded.REPO / "deps") as temp:
                directory = Path(temp)
                specs = self.create_fixtures(directory, mutate(mode))
                with patch.object(subject, "ARCHIVES", specs), self.assertRaises(ValueError):
                    subject.select(directory)

    def test_expansion_text_and_trailing_data_limits(self):
        with tempfile.TemporaryDirectory(dir=subject.bounded.REPO / "deps") as temp:
            directory = Path(temp)
            specs = self.create_fixtures(directory)
            with patch.object(subject, "ARCHIVES", specs), patch.object(subject.bounded, "EXPANSION_LIMIT", 1):
                with self.assertRaises(ValueError):
                    subject.select(directory)
            with patch.object(subject, "ARCHIVES", specs), patch.object(subject.bounded, "METADATA_LIMIT", 1):
                with self.assertRaises(ValueError):
                    subject.select(directory)
            path = directory / next(iter(specs))
            with path.open("ab") as out:
                out.write(gzip.compress(b"nonzero trailing bytes"))
            raw = path.read_bytes()
            specs[path.name]["size"] = len(raw)
            specs[path.name]["sha256"] = hashlib.sha256(raw).hexdigest()
            with patch.object(subject, "ARCHIVES", specs), self.assertRaises(ValueError):
                subject.select(directory)

    def test_selector_restored_after_failure(self):
        original = subject.bounded.candidate
        with tempfile.TemporaryDirectory(dir=subject.bounded.REPO / "deps") as temp:
            directory = Path(temp)
            specs = self.create_fixtures(directory, lambda name, members: [])
            with patch.object(subject, "ARCHIVES", specs), self.assertRaises(ValueError):
                subject.select(directory)
        self.assertIs(subject.bounded.candidate, original)


if __name__ == "__main__":
    unittest.main()
