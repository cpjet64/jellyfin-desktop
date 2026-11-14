"""Source contract tests; not executed in this source-only checkpoint."""

import io
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from offline_git import bundle_header, gitlinks


class BundleContract(unittest.TestCase):
    def test_prerequisite_is_rejected(self):
        data = b"# v2 git bundle\n-" + b"1" * 40 + b" prerequisite\n\n"
        with self.assertRaises(ValueError):
            bundle_header(io.BytesIO(data))

    def test_bundle_tip_is_recorded(self):
        data = b"# v2 git bundle\n" + b"2" * 40 + b" refs/heads/source\n\nPACK"
        stream = io.BytesIO(data)
        self.assertEqual(bundle_header(stream), {b"refs/heads/source": "2" * 40})
        self.assertEqual(stream.read(), b"PACK")

    def test_gitlinks_are_extracted_and_changed_id_differs(self):
        first = b"160000 commit " + b"3" * 40 + b"\tvendor/sub\0"
        second = b"160000 commit " + b"4" * 40 + b"\tvendor/sub\0"
        self.assertEqual(gitlinks(first), {"vendor/sub": "3" * 40})
        self.assertNotEqual(gitlinks(first), gitlinks(second))


if __name__ == "__main__":
    unittest.main()
