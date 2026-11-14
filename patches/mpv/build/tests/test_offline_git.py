"""Source contract tests; not executed in this source-only checkpoint."""

import io
import hashlib
import tempfile
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from offline_git import bundle_header, gitlinks, verify_checkout, verify_nghttp3_checkout


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

    def test_selected_nghttp3_mapping_checks_expanded_child(self):
        parent_id = "d29ceea176cebc1f41c9d818c3b1825143d3f813"
        child_id = "4b313cfd2e1b389ae632b36dcd50402307289af2"
        parent = {"name": "nghttp3", "commit": parent_id, "gitlinks": {"lib/sfparse": child_id},
                  "submodules": {"lib/sfparse": "sfparse"}}
        child = {"name": "sfparse", "commit": child_id, "gitlinks": {}, "submodules": {}}
        payload = b"child source\n"
        oid = hashlib.sha1(b"blob " + str(len(payload)).encode() + b"\0" + payload).hexdigest().encode()
        parent_tree = b"160000 commit " + child_id.encode() + b"\tlib/sfparse\0"
        child_tree = b"100644 blob " + oid + b"\tsource.c\0"
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            folder = root / "checkout" / "lib" / "sfparse"
            folder.mkdir(parents=True)
            item = folder / "source.c"
            item.write_bytes(payload)
            with mock.patch("offline_git.verified_listing", side_effect=[parent_tree, child_tree]):
                verify_nghttp3_checkout(Path("/git"), "unused", root / "p", root / "c", root,
                                        parent, child, root / "checkout")
            item.write_bytes(b"changed")
            with mock.patch("offline_git.verified_listing", side_effect=[parent_tree, child_tree]):
                with self.assertRaises(ValueError):
                    verify_nghttp3_checkout(Path("/git"), "unused", root / "p", root / "c", root,
                                            parent, child, root / "checkout")
            child["commit"] = "0" * 40
            with self.assertRaises(ValueError):
                verify_nghttp3_checkout(Path("/git"), "unused", root / "p", root / "c", root,
                                        parent, child, root / "checkout")


if __name__ == "__main__":
    unittest.main()
