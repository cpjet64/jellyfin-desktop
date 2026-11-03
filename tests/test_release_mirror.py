import hashlib
import importlib.util
import json
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "release_mirror", Path(__file__).resolve().parents[1] / "scripts" / "release_mirror.py")
mirror = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mirror)


class MirrorTests(unittest.TestCase):
    def setUp(self):
        self.release = {"tag_name": "v1", "name": "Release", "description": "Notes",
                        "commit": {"id": "abc"}, "assets": {"links": [
                            {"name": "app.zip", "url": "https://gitlab.curtpme.com/file"}]}}
        self.releases = {}
        self.assets = []
        self.fail_upload = False

    def api(self, url, token=None, method="GET", payload=None):
        if url.endswith("/git/ref/tags/v1"):
            return {"object": {"type": "commit", "sha": "abc"}}
        if method == "POST" and url.endswith("/releases"):
            self.releases["v1"] = dict(payload, id=7)
            return self.releases["v1"]
        if method == "PATCH":
            self.releases["v1"].update(payload)
            return self.releases["v1"]
        raise AssertionError((url, method))

    def request(self, url, token=None, method="GET", payload=None, content_type=None, asset=False):
        if method == "GET":
            return b"binary"
        if self.fail_upload:
            raise ValueError("interrupted upload")
        self.assets.append({"name": "app.zip", "size": len(payload),
                            "digest": "sha256:" + hashlib.sha256(payload).hexdigest()})
        return json.dumps(self.assets[-1]).encode()

    def sync(self):
        with patch.object(mirror, "api", side_effect=self.api), \
             patch.object(mirror, "request", side_effect=self.request), \
             patch.object(mirror, "pages", side_effect=lambda *_: list(self.assets)):
            mirror.sync_release(self.release, self.releases, "token")

    def test_create_and_repeat_are_idempotent(self):
        self.sync()
        self.assertFalse(self.releases["v1"]["draft"])
        with patch.object(mirror, "asset_bytes", side_effect=AssertionError("redownload")):
            self.sync()
        self.assertEqual(len(self.assets), 1)

    def test_same_host_redirect_and_unsafe_targets(self):
        handler = mirror.GitLabRedirect()
        request = mirror.urllib.request.Request("https://gitlab.curtpme.com/a")
        redirected = handler.redirect_request(request, None, 302, "", {},
                                               "https://gitlab.curtpme.com/b")
        self.assertEqual(redirected.full_url, "https://gitlab.curtpme.com/b")
        for url in ("https://example.com/", "http://gitlab.curtpme.com/",
                    "https://user@gitlab.curtpme.com/"):
            with self.assertRaises(ValueError):
                handler.redirect_request(request, None, 302, "", {}, url)

    def test_interrupted_upload_remains_draft_and_retry_publishes(self):
        self.fail_upload = True
        with self.assertRaises(ValueError):
            self.sync()
        self.assertTrue(self.releases["v1"]["draft"])
        self.fail_upload = False
        self.sync()
        self.assertFalse(self.releases["v1"]["draft"])

    def test_unowned_release_and_asset_collision_fail(self):
        self.releases["v1"] = {"id": 7, "body": "human notes"}
        with self.assertRaises(ValueError):
            self.sync()
        self.releases["v1"]["body"] = mirror.MARKER
        self.assets.append({"name": "app.zip", "size": 6, "digest": "sha256:wrong"})
        with self.assertRaises(ValueError):
            self.sync()
        self.assertEqual(len(self.assets), 1)

    def test_rejects_external_asset(self):
        self.release["assets"]["links"][0]["url"] = "https://example.com/file"
        with self.assertRaises(ValueError):
            self.sync()
        self.assertFalse(self.releases)

    def test_pagination(self):
        with patch.object(mirror, "api", side_effect=[list(range(100)), [100]]) as call:
            self.assertEqual(len(mirror.pages("https://example.test")), 101)
            self.assertIn("page=2", call.call_args.args[0])

    def test_tag_mismatch_prevents_creation(self):
        self.release["commit"]["id"] = "other"
        with self.assertRaises(ValueError):
            self.sync()
        self.assertFalse(self.releases)

    def test_annotated_tag_is_peeled(self):
        values = [{"object": {"type": "tag", "sha": "tagsha"}},
                  {"object": {"type": "commit", "sha": "abc"}}]
        with patch.object(mirror, "api", side_effect=values):
            self.assertEqual(mirror.github_commit("v1", "token"), "abc")

    def test_future_release_is_not_published(self):
        future = dict(self.release, released_at="2999-01-01T00:00:00Z")
        with patch.dict(mirror.os.environ, {"GITHUB_TOKEN": "token"}), \
             patch.object(mirror, "pages", side_effect=[[future], []]), \
             patch.object(mirror, "sync_release") as sync:
            mirror.main()
            sync.assert_not_called()

    def test_only_newest_release_is_latest(self):
        old = dict(self.release, tag_name="v1", released_at="2020-01-01T00:00:00Z")
        new = dict(self.release, tag_name="v2", released_at="2021-01-01T00:00:00Z")
        with patch.dict(mirror.os.environ, {"GITHUB_TOKEN": "token"}), \
             patch.object(mirror, "pages", side_effect=[[new, old], []]), \
             patch.object(mirror, "sync_release") as sync:
            mirror.main()
            self.assertFalse(sync.call_args_list[0].args[3])
            self.assertTrue(sync.call_args_list[1].args[3])

    def test_redirect_handler_refuses_redirects(self):
        self.assertIsNone(mirror.NoRedirect().redirect_request(
            None, None, 302, "redirect", {}, "https://example.com"))


if __name__ == "__main__":
    unittest.main()
