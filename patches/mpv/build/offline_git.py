"""Verify self-contained SHA-1 Git bundles in a fresh private bare repository.

Source only: this implementation has not been executed or qualified.
"""

import hashlib
import os
import re
import subprocess
from pathlib import Path

OID = re.compile(r"[0-9a-f]{40}\Z")
SHA256 = re.compile(r"[0-9a-f]{64}\Z")


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def bundle_header(stream):
    version = stream.readline(128)
    if version not in (b"# v2 git bundle\n", b"# v3 git bundle\n"):
        raise ValueError("unsupported Git bundle header")
    refs = {}
    for _ in range(4096):
        line = stream.readline(8193)
        if not line or len(line) > 8192:
            raise ValueError("invalid Git bundle header")
        if line == b"\n":
            return refs
        if line.startswith(b"-"):
            raise ValueError("bundle has prerequisites; self-contained bundle required")
        if line.startswith(b"@"):
            if version != b"# v3 git bundle\n" or line != b"@object-format=sha1\n":
                raise ValueError("unsupported bundle capability")
            continue
        oid, separator, ref = line.rstrip(b"\n").partition(b" ")
        if not separator or not re.fullmatch(b"[0-9a-f]{40}", oid) or not ref.startswith(b"refs/"):
            raise ValueError("invalid bundle ref")
        if ref in refs:
            raise ValueError("duplicate bundle ref")
        refs[ref] = oid.decode("ascii")
    raise ValueError("bundle header exceeds ref limit")


def run_git(git, repo, env, arguments, stdin=None):
    result = subprocess.run([str(git), "--git-dir", str(repo), "-c", "core.hooksPath=" + os.devnull,
                             "-c", "gc.auto=0", *arguments], stdin=stdin,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            env=env, timeout=300, check=False)
    if result.returncode:
        raise ValueError(f"Git verification failed: {arguments[0]}")
    return result.stdout


def gitlinks(raw):
    result = {}
    for entry in raw.split(b"\0"):
        if not entry:
            continue
        metadata, separator, name = entry.partition(b"\t")
        fields = metadata.split(b" ")
        if not separator or len(fields) != 3:
            raise ValueError("malformed Git tree listing")
        if fields[0] == b"160000":
            path = name.decode("utf-8", errors="strict")
            oid = fields[2].decode("ascii", errors="strict")
            if fields[1] != b"commit" or not OID.fullmatch(oid) or path in result:
                raise ValueError("invalid Git submodule entry")
            result[path] = oid
    return result


def verify_bundle(git, expected_git_sha, bundle, checkdir, locked):
    if not git.is_absolute() or not SHA256.fullmatch(expected_git_sha) or digest(git) != expected_git_sha:
        raise ValueError("Git executable identity differs from approved lock")
    commit, tree, expected_links = locked.get("commit"), locked.get("tree"), locked.get("gitlinks")
    if not isinstance(commit, str) or not OID.fullmatch(commit) or not isinstance(tree, str) or not OID.fullmatch(tree):
        raise ValueError("missing commit or tree identity")
    if not isinstance(expected_links, dict) or any(not isinstance(k, str) or not isinstance(v, str) or
                                                   not OID.fullmatch(v) for k, v in expected_links.items()):
        raise ValueError("missing selected Git tree gitlink map")
    checkdir.mkdir(mode=0o700, parents=False, exist_ok=False)
    repo = checkdir / "objects.git"
    template = checkdir / "empty-template"
    template.mkdir(mode=0o700)
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith("GIT_")}
    env.update({"HOME": str(checkdir), "GIT_CONFIG_NOSYSTEM": "1", "GIT_CONFIG_GLOBAL": os.devnull,
                "GIT_TERMINAL_PROMPT": "0", "GIT_NO_REPLACE_OBJECTS": "1"})
    with bundle.open("rb", buffering=0) as stream:
        refs = bundle_header(stream)
        if commit not in refs.values():
            raise ValueError("locked commit is not an advertised bundle tip")
        subprocess.run([str(git), "init", "--bare", "--template=" + str(template), str(repo)],
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, timeout=30, check=True)
        run_git(git, repo, env, ["bundle", "verify", str(bundle)])
        advertised = run_git(git, repo, env, ["bundle", "list-heads", str(bundle)])
        listed = {}
        for line in advertised.splitlines():
            oid, separator, ref = line.partition(b" ")
            if not separator or ref in listed:
                raise ValueError("invalid advertised bundle refs")
            listed[ref] = oid.decode("ascii", errors="strict")
        if listed != refs:
            raise ValueError("bundle ref listing differs from parsed header")
        run_git(git, repo, env, ["index-pack", "--stdin"], stdin=stream)
    run_git(git, repo, env, ["fsck", "--full", "--strict", "--no-reflogs", commit])
    actual_tree = run_git(git, repo, env, ["rev-parse", "--verify", commit + "^{tree}"]).strip().decode("ascii")
    if actual_tree != tree:
        raise ValueError("commit tree differs from source lock")
    listing = run_git(git, repo, env, ["ls-tree", "-r", "-z", "--full-tree", commit])
    if gitlinks(listing) != expected_links:
        raise ValueError("Git tree submodule identities differ from source lock")
