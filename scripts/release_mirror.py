"""Mirror published GitLab releases to the matching GitHub repository."""
import datetime as dt
import hashlib
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request

GITLAB = "https://gitlab.curtpme.com/api/v4/projects/314"
GITHUB = "https://api.github.com/repos/cpjet64/jellyfin-desktop"
UPLOAD = "https://uploads.github.com/repos/cpjet64/jellyfin-desktop"
MARKER = "<!-- mirrored-from-gitlab-project-314 -->"
MAX_ASSET = 512 * 1024 * 1024


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def request(url, token=None, method="GET", payload=None, content_type=None):
    headers = {"User-Agent": "jellyfin-release-mirror", "Accept": "application/vnd.github+json"}
    if token:
        headers["Authorization"] = "Bearer " + token
        headers["X-GitHub-Api-Version"] = "2022-11-28"
    if isinstance(payload, dict):
        payload = json.dumps(payload).encode()
        content_type = "application/json"
    if content_type:
        headers["Content-Type"] = content_type
    req = urllib.request.Request(url, data=payload, headers=headers, method=method)
    with urllib.request.build_opener(NoRedirect).open(req, timeout=120) as response:
        length = response.headers.get("Content-Length")
        if length and int(length) > MAX_ASSET:
            raise ValueError("Response exceeds maximum asset size")
        data = response.read(MAX_ASSET + 1)
        if len(data) > MAX_ASSET:
            raise ValueError("Response exceeds maximum asset size")
        return data


def api(url, token=None, method="GET", payload=None):
    return json.loads(request(url, token, method, payload))


def pages(url, token=None):
    result = []
    for page in range(1, 10001):
        separator = "&" if "?" in url else "?"
        items = api(f"{url}{separator}per_page=100&page={page}", token)
        if not isinstance(items, list):
            raise ValueError("Expected a paginated list")
        result.extend(items)
        if len(items) < 100:
            return result
    raise ValueError("Pagination limit exceeded")


def quote(value):
    return urllib.parse.quote(value, safe="")


def github_commit(tag, token):
    ref = api(f"{GITHUB}/git/ref/tags/{quote(tag)}", token)["object"]
    for _ in range(8):
        if ref["type"] == "commit":
            return ref["sha"]
        if ref["type"] != "tag":
            raise ValueError("Tag does not resolve to a commit")
        ref = api(f"{GITHUB}/git/tags/{ref['sha']}", token)["object"]
    raise ValueError("Tag nesting limit exceeded")


def asset_bytes(url):
    parsed = urllib.parse.urlsplit(url)
    if (parsed.scheme != "https" or parsed.netloc != "gitlab.curtpme.com" or
            parsed.username or parsed.password or parsed.fragment):
        raise ValueError("Release asset must use the intended GitLab HTTPS host")
    return request(url)


def desired_assets(release):
    links = release.get("assets", {}).get("links", [])
    names = set()
    result = []
    for link in links:
        name = link["name"]
        if not name or name in names or "/" in name or "\\" in name:
            raise ValueError("Invalid or duplicate release asset name")
        names.add(name)
        data = asset_bytes(link["url"])
        result.append((name, data, "sha256:" + hashlib.sha256(data).hexdigest()))
    return result


def sync_release(release, existing, token):
    tag = release["tag_name"]
    if github_commit(tag, token) != release["commit"]["id"]:
        raise ValueError(f"Tag target mismatch for {tag}")
    data = desired_assets(release)
    body = (release.get("description") or "") + "\n\n" + MARKER
    target = existing.get(tag)
    if target and MARKER not in (target.get("body") or ""):
        raise ValueError(f"Unowned GitHub release collision: {tag}")
    if not target:
        target = api(f"{GITHUB}/releases", token, "POST", {
            "tag_name": tag, "name": release.get("name") or tag,
            "body": body, "draft": True})
        existing[tag] = target
    uploaded = {a["name"]: a for a in pages(f"{GITHUB}/releases/{target['id']}/assets", token)}
    for name, content, digest in data:
        if name in uploaded:
            if uploaded[name].get("digest") != digest or uploaded[name].get("size") != len(content):
                raise ValueError(f"Asset collision or unverifiable digest: {tag}/{name}")
            continue
        response = json.loads(request(
            f"{UPLOAD}/releases/{target['id']}/assets?name={quote(name)}", token,
            "POST", content, "application/octet-stream"))
        if response.get("digest") != digest or response.get("size") != len(content):
            raise ValueError(f"Uploaded asset digest mismatch or unavailable: {tag}/{name}")
    api(f"{GITHUB}/releases/{target['id']}", token, "PATCH", {
        "name": release.get("name") or tag, "body": body, "draft": False})


def main():
    token = os.environ["GITHUB_TOKEN"]
    releases = pages(f"{GITLAB}/releases")
    existing = {r["tag_name"]: r for r in pages(f"{GITHUB}/releases", token)}
    now = dt.datetime.now(dt.timezone.utc)
    for release in releases:
        published = dt.datetime.fromisoformat(release["released_at"].replace("Z", "+00:00"))
        if published <= now:
            sync_release(release, existing, token)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, urllib.error.URLError) as exc:
        print(f"Release mirror failed: {exc}", file=sys.stderr)
        sys.exit(1)
