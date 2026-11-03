"""Install pinned official Linux tools into a new CI-job-owned prefix."""

import hashlib
from pathlib import Path
import urllib.request

from safe_extract import extract_tar, extract_zip

TOOLS = {
    # https://cmake.org/files/v4.1/cmake-4.1.2-SHA-256.txt
    "cmake": ("https://cmake.org/files/v4.1/cmake-4.1.2-linux-x86_64.tar.gz", "773cc679c3a7395413bd096523f8e5d6c39f8718af4e12eb4e4195f72f35e4ab"),
    # https://github.com/ninja-build/ninja/releases/tag/v1.13.2 (asset digest)
    "ninja": ("https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-linux.zip", "5749cbc4e668273514150a80e387a957f933c6ed3f5f11e03fb30955e2bbead6"),
    # https://nodejs.org/dist/v22.21.1/SHASUMS256.txt
    "node": ("https://nodejs.org/dist/v22.21.1/node-v22.21.1-linux-x64.tar.xz", "680d3f30b24a7ff24b98db5e96f294c0070f8f9078df658da1bce1b9c9873c88"),
}


def download(url, digest, path):
    hasher = hashlib.sha256()
    count = 0
    with urllib.request.urlopen(url, timeout=120) as response, path.open("xb") as output:
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk:
                break
            count += len(chunk)
            if count > 200 * 1024 * 1024:
                raise ValueError("Tool download exceeds 200 MiB")
            hasher.update(chunk)
            output.write(chunk)
    if hasher.hexdigest() != digest:
        raise ValueError("Tool download SHA-256 mismatch")


def install(name, workspace):
    url, digest = TOOLS[name]
    archive = workspace / f"{name}.archive"
    destination = workspace / name
    download(url, digest, archive)
    destination.mkdir(exist_ok=False)
    if name == "ninja":
        extract_zip(archive, destination)
        executable = destination / "ninja"
    elif name == "node":
        member = "node-v22.21.1-linux-x64/bin/node"
        extract_tar(archive, destination, member)
        executable = destination / member
    else:
        extract_tar(archive, destination)
        executable = destination / "cmake-4.1.2-linux-x86_64/bin/cmake"
    if not executable.is_file():
        raise ValueError(f"Missing bootstrapped executable: {name}")
    executable.chmod(executable.stat().st_mode | 0o100)
    return executable.parent


def bootstrap(missing, workspace):
    workspace = Path(workspace)
    workspace.mkdir(parents=True, exist_ok=False)
    bins = []
    for name in ("cmake", "ninja", "node"):
        if name in missing:
            print(f"Bootstrapping {name} from pinned official archive", flush=True)
            bins.append(install(name, workspace))
    return bins
