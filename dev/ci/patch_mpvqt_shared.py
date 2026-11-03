"""Apply the reviewed destructor-only patch to a pristine MpvQt build copy."""

import hashlib
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT / "patches/mpvqt/0002-shared-destructor.patch"
PATCH_HASH = "58c503f0f903df52b5899171fcc199ee063719d1d5bd60ef8ca9219c524b6ee5"
FILES = {
    "src/mpvcontroller.h": (
        "eede2d838a167de76fe801804dcb3c6cc9094b02c437a9b551c6d0802bb474a8",
        "10f1fec25bc4e7b24977a69a110e37842a3c7496e8e5dad1fab8f64f537fd32d"),
    "src/mpvcontroller.cpp": (
        "0c2d0d9c091cee55c497f63fc13b00eead20767eca2f75575b601d471e0fe999",
        "7f2ad5088698a9f7e7c613ade77f3be46849525a78682188a8739ba610740895"),
}
CRLF_HASHES = {
    "src/mpvcontroller.h": "a2e98d08c9f2240d767f633aa0524e6344858ee7c272ccf1e6902f32a873cb56",
    "src/mpvcontroller.cpp": "6e3afcfcde1a81f123c7774cd94b56219ebadd8977795af3ef8e17738a6f31fe",
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def apply(source):
    source = Path(source).absolute()
    if source.is_symlink() or not source.is_dir():
        raise ValueError("MpvQt build copy must be a real directory")
    source = source.resolve(strict=True)
    git_marker = source / ".git"
    source_dir = source / "src"
    if git_marker.exists() or git_marker.is_symlink() or source_dir.is_symlink():
        raise ValueError("MpvQt build copy must not be a checkout or linked source")
    patch = PATCH.read_bytes().replace(b"\r\n", b"\n")
    if sha(patch) != PATCH_HASH:
        raise ValueError("Shared MpvQt compatibility patch hash mismatch")
    contents = {}
    for name, (before, _) in FILES.items():
        path = source / name
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"MpvQt source hash mismatch: {name}")
        raw = path.read_bytes()
        normalized = raw.replace(b"\r\n", b"\n")
        if sha(raw) not in (before, CRLF_HASHES[name]) or sha(normalized) != before:
            raise ValueError(f"MpvQt source hash mismatch: {name}")
        print(f"{name}: input SHA256 {sha(raw)}")
        contents[name] = normalized
    for name, data in contents.items():
        (source / name).write_bytes(data)
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    env = os.environ.copy()
    env["GIT_CEILING_DIRECTORIES"] = str(source.parent)
    result = subprocess.run(["git", "apply", "--no-index", "--unidiff-zero", "--whitespace=nowarn", "-"],
                            cwd=source, input=patch, capture_output=True,
                            timeout=60, creationflags=flags, check=False, env=env)
    if result.returncode:
        raise RuntimeError("MpvQt compatibility patch failed: " + result.stderr.decode(errors="replace"))
    for name, (before, after) in FILES.items():
        path = source / name
        raw = path.read_bytes()
        normalized = raw.replace(b"\r\n", b"\n")
        if sha(normalized) != after:
            raise ValueError(f"Patched MpvQt source hash mismatch: {name}")
        print(f"{name}: patch output SHA256 {sha(raw)}")
        path.write_bytes(normalized)
        if sha(path.read_bytes()) != after:
            raise ValueError(f"Written MpvQt source hash mismatch: {name}")
        print(f"{name}: {before} -> {after}")
    print(f"MpvQt 2ebbd38 + destructor compatibility patch SHA256: {PATCH_HASH}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: patch_mpvqt_shared.py <pristine-build-copy>")
    apply(sys.argv[1])
