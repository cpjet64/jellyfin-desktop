"""Source contract tests; not executed in this source-only checkpoint."""

import io
import hashlib
import tempfile
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from offline_git import bundle_header, gitlinks, verify_checkout


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

    def test_checkout_rejects_changed_and_extra_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            checkout = root / "source"
            checkout.mkdir()
            payload = b"locked source\n"
            item = checkout / "source.c"
            item.write_bytes(payload)
            oid = hashlib.sha1(b"blob " + str(len(payload)).encode() + b"\0" + payload).hexdigest()
            listing = b"100644 blob " + oid.encode() + b"\tsource.c\0"
            bundle = root / "source.bundle"
            bundle.write_bytes(b"bundle")
            locked = {"bundle_sha256": hashlib.sha256(b"bundle").hexdigest()}
            with mock.patch("offline_git.verify_bundle", return_value=listing):
                verify_checkout(Path("/git"), "unused", bundle, root / "check", locked, checkout)
                item.write_bytes(b"modified\n")
                with self.assertRaises(ValueError):
                    verify_checkout(Path("/git"), "unused", bundle, root / "check", locked, checkout)
                item.write_bytes(payload)
                (checkout / "extra").write_bytes(b"extra")
                with self.assertRaises(ValueError):
                    verify_checkout(Path("/git"), "unused", bundle, root / "check", locked, checkout)


if __name__ == "__main__":
    unittest.main()
