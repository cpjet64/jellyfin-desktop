import hashlib
import http.client
import importlib.util
import io
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
        self.content = b"binary"
        self.etag = '"one"'
        self.patches = []
        self.deleted = []

    def api(self, url, token=None, method="GET", payload=None):
        if url.endswith("/git/ref/tags/v1"):
            return {"object": {"type": "commit", "sha": "abc"}}
        if method == "POST" and url.endswith("/releases"):
            self.releases["v1"] = dict(payload, id=7)
            return self.releases["v1"]
        if method == "PATCH":
            self.patches.append(dict(payload))
            self.releases["v1"].update(payload)
            return self.releases["v1"]
        raise AssertionError((url, method))

    def request(self, url, token=None, method="GET", payload=None, content_type=None, asset=False):
        if method == "GET":
            return b"binary"
        if method == "DELETE":
            asset_id = int(url.rsplit("/", 1)[1])
            self.deleted.append(asset_id)
            self.assets[:] = [item for item in self.assets if item.get("id") != asset_id]
            return b""
        if self.fail_upload:
            raise ValueError("interrupted upload")
        self.assets.append({"name": "app.zip", "state": "uploaded", "size": len(payload),
                            "digest": "sha256:" + hashlib.sha256(payload).hexdigest()})
        return json.dumps(self.assets[-1]).encode()

    def sync(self):
        with patch.object(mirror, "api", side_effect=self.api), \
             patch.object(mirror, "request", side_effect=self.request), \
             patch.object(mirror, "pages", side_effect=lambda *_: list(self.assets)), \
             patch.object(mirror, "asset_bytes", side_effect=self.fetch):
            mirror.sync_release(self.release, self.releases, "token")

    def fetch(self, url, etag=None):
        if not mirror.valid_asset_url(url):
            raise ValueError("unsafe URL")
        if etag and etag == self.etag:
            return None, etag
        return self.content, self.etag

    def test_create_and_repeat_are_idempotent(self):
        self.sync()
        self.assertFalse(self.releases["v1"]["draft"])
        self.patches.clear()
        self.sync()
        self.assertEqual(self.patches, [])
        self.assertEqual(len(self.assets), 1)

    def test_same_url_changed_bytes_fail_without_writes(self):
        self.sync()
        self.content, self.etag = b"different", '"two"'
        self.patches.clear()
        with self.assertRaises(ValueError):
            self.sync()
        self.assertEqual(self.patches, [])

    def test_failed_upload_starter_is_replaced_and_verified(self):
        self.fail_upload = True
        with self.assertRaises(ValueError):
            self.sync()
        self.assets.append({"name": "app.zip", "state": "starter", "size": 0,
                            "digest": None, "id": 91})
        self.fail_upload = False
        self.sync()
        self.assertEqual(self.deleted, [91])
        self.assertEqual(self.assets[0]["state"], "uploaded")
        self.assertFalse(self.releases["v1"]["draft"])

    def test_nonempty_or_unowned_starter_is_preserved(self):
        self.releases["v1"] = {"id": 7, "body": "human release", "draft": True}
        self.assets.append({"name": "app.zip", "state": "starter", "size": 0,
                            "digest": None, "id": 91})
        with self.assertRaises(ValueError):
            self.sync()
        self.releases["v1"]["body"] = mirror.MARKER
        self.assets[0]["size"] = 1
        with self.assertRaises(ValueError):
            self.sync()
        self.assertEqual(self.deleted, [])

    def test_no_etag_downloads_each_time(self):
        self.etag = None
        self.sync()
        with patch.object(self, "fetch", wraps=self.fetch) as fetch:
            self.sync()
            self.assertIsNone(fetch.call_args.args[1])

    def test_conditional_304_requires_strong_etag(self):
        from urllib.error import HTTPError
        from unittest.mock import MagicMock
        opener = MagicMock()
        opener.open.side_effect = HTTPError("https://gitlab.curtpme.com/file", 304,
                                          "Not Modified", {}, None)
        with patch.object(mirror.urllib.request, "build_opener", return_value=opener):
            self.assertEqual(mirror.asset_bytes("https://gitlab.curtpme.com/file", '"v1"'),
                             (None, '"v1"'))
            self.assertEqual(opener.open.call_args.args[0].get_header("If-none-match"), '"v1"')
            with self.assertRaises(HTTPError):
                mirror.asset_bytes("https://gitlab.curtpme.com/file", 'W/"v1"')
            self.assertIsNone(opener.open.call_args.args[0].get_header("If-none-match"))

    def test_real_http_response_rejects_truncation_before_mutation(self):
        from unittest.mock import MagicMock

        class Socket:
            def __init__(self, length):
                self.stream = io.BytesIO(
                    f"HTTP/1.1 200 OK\r\nContent-Length: {length}\r\n\r\n".encode() + b"abc")

            def makefile(self, *args, **kwargs):
                return self.stream

        def response(length):
            result = http.client.HTTPResponse(Socket(length))
            result.begin()
            return result

        opener = MagicMock()
        opener.open.side_effect = lambda *args, **kwargs: response(10)
        with patch.object(mirror.urllib.request, "build_opener", return_value=opener), \
             patch.object(mirror, "api", side_effect=self.api), \
             patch.object(mirror, "request", side_effect=self.request) as request, \
             patch.object(mirror, "pages", return_value=[]):
            with self.assertRaisesRegex(ValueError, "incomplete"):
                mirror.sync_release(self.release, self.releases, "token")
            request.assert_not_called()
            self.assertEqual(self.releases, {})
            self.assertEqual(self.patches, [])
        self.releases["v1"] = {"id": 7, "body": mirror.MARKER, "draft": False}
        starter = {"name": "app.zip", "state": "starter", "size": 0,
                   "digest": None, "id": 91}
        with patch.object(mirror.urllib.request, "build_opener", return_value=opener), \
             patch.object(mirror, "api", side_effect=self.api), \
             patch.object(mirror, "request") as request, \
             patch.object(mirror, "pages", return_value=[starter]):
            with self.assertRaisesRegex(ValueError, "incomplete"):
                mirror.sync_release(self.release, self.releases, "token")
            request.assert_not_called()
            self.assertEqual(self.patches, [])
        opener.open.side_effect = lambda *args, **kwargs: response(3)
        with patch.object(mirror.urllib.request, "build_opener", return_value=opener):
            self.assertEqual(mirror.asset_bytes("https://gitlab.curtpme.com/file"), (b"abc", None))

    def test_removed_asset_fails_without_writes(self):
        self.sync()
        self.release["assets"]["links"] = []
        self.patches.clear()
        with self.assertRaises(ValueError):
            self.sync()
        self.assertEqual(self.patches, [])

    def test_existing_published_upload_failure_drafts_then_retry(self):
        self.sync()
        self.release["assets"]["links"].append(
            {"name": "other.zip", "url": "https://gitlab.curtpme.com/other"})
        self.fail_upload = True
        with self.assertRaises(ValueError):
            self.sync()
        self.assertTrue(self.releases["v1"]["draft"])
        self.fail_upload = False
        # Fixture upload reports the second asset under its requested name.
        original = self.request
        def upload(url, *args, **kwargs):
            result = original(url, *args, **kwargs)
            if "other.zip" in url:
                self.assets[-1]["name"] = "other.zip"
                return json.dumps(self.assets[-1]).encode()
            return result
        with patch.object(self, "request", side_effect=upload):
            self.sync()
        self.assertFalse(self.releases["v1"]["draft"])

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
             patch.object(mirror, "api", return_value={"id": 1}), \
             patch.object(mirror, "sync_release") as sync:
            mirror.main()
            sync.assert_not_called()

    def test_only_newest_release_is_latest(self):
        old = dict(self.release, tag_name="v1", released_at="2020-01-01T00:00:00Z")
        new = dict(self.release, tag_name="v2", released_at="2021-01-01T00:00:00Z")
        with patch.dict(mirror.os.environ, {"GITHUB_TOKEN": "token"}), \
             patch.object(mirror, "pages", side_effect=[[new, old], []]), \
             patch.object(mirror, "api", return_value={"id": 1}), \
             patch.object(mirror, "sync_release") as sync:
            mirror.main()
            self.assertFalse(sync.call_args_list[0].args[3])
            self.assertTrue(sync.call_args_list[1].args[3])

    def test_redirect_handler_refuses_redirects(self):
        self.assertIsNone(mirror.NoRedirect().redirect_request(
            None, None, 302, "redirect", {}, "https://example.com"))


if __name__ == "__main__":
    unittest.main()
