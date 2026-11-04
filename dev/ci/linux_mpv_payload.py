"""Prepare the approved split mpv SDK in a fresh job-owned directory."""
import hashlib
import io
from pathlib import Path
import subprocess
import tarfile
import urllib.request

from explicit_mpv import SHA, explicit_inputs

URL = "https://archive.ubuntu.com/ubuntu/pool/universe/m/mpv/libmpv-dev_0.37.0-1ubuntu4_amd64.deb"
SIZE = 42316
HEADERS = ("client.h", "render.h", "render_gl.h", "stream_cb.h")
LIBRARY = "/usr/lib/x86_64-linux-gnu/libmpv.so.2"


def extract_headers(archive, destination):
    data = subprocess.run(["dpkg-deb", "--fsys-tarfile", str(archive)], check=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30).stdout
    if len(data) > 2 * 1024 * 1024:
        raise RuntimeError("BLOCKED: unexpected mpv archive expansion")
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:") as tar:
        members = tar.getmembers()
        for name in HEADERS:
            path = "usr/include/mpv/" + name
            matches = [m for m in members if m.name.removeprefix("./") == path]
            if len(matches) != 1 or not matches[0].isfile() or matches[0].size > 1024 * 1024:
                raise RuntimeError("BLOCKED: invalid mpv header " + name)
            payload = tar.extractfile(matches[0]).read()
            (destination / name).write_bytes(payload)
            print(f"mpv header {name} sha256={hashlib.sha256(payload).hexdigest()}", flush=True)


def acquire_mpv(job_root):
    root = Path(job_root).resolve(strict=True)
    if not root.is_dir():
        raise RuntimeError("BLOCKED: job root is not a directory")
    owned = root / "mpv-sdk"
    owned.mkdir(exist_ok=False)
    archive = owned / "libmpv-dev.deb"
    with urllib.request.urlopen(URL, timeout=60) as response, archive.open("xb") as output:
        payload = response.read(SIZE + 1)
        if len(payload) != SIZE or hashlib.sha256(payload).hexdigest() != SHA:
            raise RuntimeError("BLOCKED: mpv development package size or SHA mismatch")
        output.write(payload)
    include = owned / "include"
    (include / "mpv").mkdir(parents=True)
    extract_headers(archive, include / "mpv")
    values = {"CI_MPV_DEV_DEB": str(archive), "CI_MPV_INCLUDE_DIR": str(include),
              "CI_MPV_LIBRARY": LIBRARY, "CI_MPV_PINNED_RUNTIME": "1"}
    explicit_inputs(values, "Linux", root)
    print("mpv development package sha256=" + SHA, flush=True)
    return values
