"""Staged acquisition of the two reviewed macOS helper assets.

Not a launcher or static qualification. Call only after independent review and
publication of this harness; use an otherwise idle, private task-owned parent.
"""
import json
import hashlib
import os
from pathlib import Path
import shutil
import urllib.request
from urllib.parse import urlsplit
import sys

from mac_helpers import checked_copy, extract_deno, storage_exhausted

MANIFEST = Path(__file__).with_name("mac_helpers_manifest.json")
ALLOWED = {"github.com", "release-assets.githubusercontent.com",
           "objects.githubusercontent.com", "github-releases.githubusercontent.com"}


def validate_url(url):
    parsed = urlsplit(url)
    if (parsed.scheme != "https" or parsed.hostname not in ALLOWED or
            parsed.username or parsed.password or parsed.port not in (None, 443)):
        raise ValueError("unapproved acquisition URL")


class RestrictedRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        validate_url(newurl)
        return super().redirect_request(req, fp, code, msg, headers, newurl)


def official_open(url, timeout):
    validate_url(url)
    return urllib.request.build_opener(RestrictedRedirect()).open(url, timeout=timeout)


def verified_paths(workspace):
    """Resolve helpers only from a completed stage, rechecking every byte."""
    workspace = Path(workspace)
    if not (workspace / "READY").is_file():
        raise ValueError("acquisition is not ready")
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    paths = (workspace / "yt_dlp", workspace / "extracted" / "deno")
    expected = (manifest["yt_dlp"]["sha256"], manifest["deno_executable"]["sha256"])
    for path, digest in zip(paths, expected):
        if path.is_symlink() or not path.is_file():
            raise ValueError("helper is not a regular file")
        with path.open("rb") as source:
            actual = hashlib.file_digest(source, "sha256").hexdigest()
        if actual != digest:
            raise ValueError("helper digest mismatch")
    return paths


def check_parent(parent):
    if not parent.is_absolute() or not parent.is_dir():
        raise ValueError("parent must be an existing absolute directory")
    if parent.resolve(strict=True) != parent:
        raise ValueError("parent path must be canonical and contain no links")
    info = parent.stat()
    if info.st_uid != os.getuid() or info.st_mode & 0o077:
        raise ValueError("parent must be owned by the current user and private")
    for ancestor in parent.parents:
        info = ancestor.stat()
        if info.st_uid not in (0, os.getuid()) or info.st_mode & 0o022:
            raise ValueError("ancestor must be controlled and not group/world writable")


def acquire(parent, project_root, committed_bytes):
    """Acquire on macOS; committed_bytes reserves other concurrent obligations."""
    if sys.platform != "darwin":
        raise RuntimeError("native acquisition is restricted to macOS")
    parent = Path(parent)
    project_root = Path(project_root)
    deps = project_root / "deps"
    if (not project_root.is_absolute() or project_root.resolve(strict=True) != project_root
            or not (project_root / ".git").exists() or not parent.is_relative_to(deps)
            or parent == deps):
        raise ValueError("workspace must be inside the canonical repository deps directory")
    check_parent(parent)
    return _acquire(parent, committed_bytes, official_open, deps / ".mac-helper-acquisition.lock")


def _acquire(parent, committed_bytes, opener, lease=None):
    """Use only a private reserved parent; READY marks complete verification."""
    parent = Path(parent)
    if any(parent.iterdir()):
        raise ValueError("parent must be empty and reserved for this acquisition")
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    limits = manifest["limits"]
    planned = sum(manifest[name]["size"] for name in ("yt_dlp", "deno_zip"))
    metadata = manifest["deno_executable"]["checksum_bytes"]
    if metadata > limits["metadata"] or planned + metadata > limits["acquisition"]:
        raise ValueError("acquisition budget exceeded")
    if planned + metadata + limits["deno_expanded"] > limits["workspace"]:
        raise ValueError("workspace budget exceeded")
    if not isinstance(committed_bytes, int) or committed_bytes < 0:
        raise ValueError("concurrent commitment must be a nonnegative integer")
    lock = lease if lease is not None else parent / ".mac-helper-acquisition.lock"
    stage = parent / "mac-helpers"
    lock_fd = os.open(lock, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    created_stage = False
    storage_failure = False
    try:
        if shutil.disk_usage(parent).free - committed_bytes < limits["free_space"]:
            raise ValueError("insufficient free space")
        stage.mkdir(mode=0o700)
        created_stage = True
        for name in ("yt_dlp", "deno_zip"):
            item = manifest[name]
            with opener(item["url"], timeout=60) as response:
                validate_url(response.geturl())
                checked_copy(response, stage / name, item["size"], item["sha256"])
        checksum = manifest["deno_executable"]
        with opener(checksum["checksum_url"], timeout=60) as response:
            validate_url(response.geturl())
            checked_copy(response, stage / "deno.sha256sum",
                         checksum["checksum_bytes"], checksum["checksum_sha256"])
        extract_deno(stage / "deno_zip", stage / "extracted", checksum["sha256"],
                     limits["deno_expanded"])
        # Close the marker before atomically linking it without replacement.
        marker = stage / ".READY.pending"
        with open(marker, "xb"):
            pass
        os.link(marker, stage / "READY")
        created_stage = False
        return stage
    except BaseException as exc:
        storage_failure = storage_exhausted(exc)
        raise
    finally:
        if created_stage and not storage_failure:
            shutil.rmtree(stage)
        os.close(lock_fd)
        if not storage_failure:
            lock.unlink()
