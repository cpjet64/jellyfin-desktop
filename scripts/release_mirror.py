"""Mirror published GitLab releases to the matching GitHub repository."""
import datetime as dt
import base64
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
MANIFEST = "<!-- gitlab-asset-manifest:"
MAX_ASSET = 512 * 1024 * 1024


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def valid_asset_url(url):
    parsed = urllib.parse.urlsplit(url)
    return (parsed.scheme == "https" and parsed.netloc == "gitlab.curtpme.com" and
            not parsed.username and not parsed.password and not parsed.fragment)


class GitLabRedirect(urllib.request.HTTPRedirectHandler):
    max_redirections = 5
    max_repeats = 2

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        if not valid_asset_url(newurl):
            raise ValueError("Asset redirect must remain on the intended GitLab HTTPS host")
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def request(url, token=None, method="GET", payload=None, content_type=None, asset=False):
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
    redirect = GitLabRedirect() if asset else NoRedirect()
    with urllib.request.build_opener(redirect).open(req, timeout=120) as response:
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
    if not valid_asset_url(url):
        raise ValueError("Release asset must use the intended GitLab HTTPS host")
    return request(url, asset=True)


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


def fingerprint(release):
    links = release.get("assets", {}).get("links", [])
    raw = json.dumps([(x["name"], x["url"]) for x in links],
                     ensure_ascii=True, separators=(",", ":")).encode()
    return hashlib.sha256(raw).hexdigest()


def read_manifest(body):
    try:
        encoded = body.split(MANIFEST, 1)[1].split(" -->", 1)[0]
        return json.loads(base64.b64decode(encoded, validate=True))
    except (IndexError, ValueError, TypeError):
        return {}


def write_body(description, manifest):
    encoded = base64.b64encode(json.dumps(manifest, sort_keys=True).encode()).decode()
    return (description or "") + "\n\n" + MARKER + "\n" + MANIFEST + encoded + " -->"


def sync_release(release, existing, token, latest=False):
    tag = release["tag_name"]
    if github_commit(tag, token) != release["commit"]["id"]:
        raise ValueError(f"Tag target mismatch for {tag}")
    target = existing.get(tag)
    if target and MARKER not in (target.get("body") or ""):
        raise ValueError(f"Unowned GitHub release collision: {tag}")
    uploaded = {} if not target else {
        a["name"]: a for a in pages(f"{GITHUB}/releases/{target['id']}/assets", token)}
    old = read_manifest(target.get("body", "")) if target else {}
    source = fingerprint(release)
    assets = old.get("assets", {})
    link_names = [link["name"] for link in release.get("assets", {}).get("links", [])]
    unchanged = (old.get("source") == source and len(link_names) == len(set(link_names)) and
                 set(assets) == set(link_names) and
                 all(name in uploaded and uploaded[name].get("digest") == value["digest"] and
                     uploaded[name].get("size") == value["size"]
                     for name, value in assets.items()))
    data = [] if unchanged else desired_assets(release)
    if not unchanged:
        assets = {name: {"digest": digest, "size": len(content)}
                  for name, content, digest in data}
    body = write_body(release.get("description"), {"source": source, "assets": assets})
    if not target:
        target = api(f"{GITHUB}/releases", token, "POST", {
            "tag_name": tag, "name": release.get("name") or tag,
            "body": body, "draft": True})
        existing[tag] = target
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
        "name": release.get("name") or tag, "body": body, "draft": False,
        "make_latest": "true" if latest else "false"})
    return not unchanged


def main():
    token = os.environ["GITHUB_TOKEN"]
    releases = pages(f"{GITLAB}/releases")
    existing = {r["tag_name"]: r for r in pages(f"{GITHUB}/releases", token)}
    now = dt.datetime.now(dt.timezone.utc)
    published = [(dt.datetime.fromisoformat(r["released_at"].replace("Z", "+00:00")), r)
                 for r in releases]
    published = sorted(((date, r) for date, r in published if date <= now),
                       key=lambda item: (item[0], item[1]["tag_name"]))
    changed = 0
    for index, (_, release) in enumerate(published):
        changed += bool(sync_release(release, existing, token, index == len(published) - 1))
    print(f"Published releases checked: {len(published)}; asset manifests refreshed: {changed}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, urllib.error.URLError) as exc:
        print(f"Release mirror failed: {exc}", file=sys.stderr)
        sys.exit(1)
