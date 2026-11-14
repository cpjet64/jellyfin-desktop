"""Proposed offline provider overlay planner. Execution requires separate approval."""

import hashlib
import os
import shutil
from pathlib import Path
import recipe_edits

PROVIDER = "1f8d2e76255abc48f0456d9be99b4cab9c6566e6"
ORIGINALS = {
    "toolchain/rustup.cmake": "5ec4d394c320716e66215f0c83d62733e603a2e5d2e97e2bed7ebc427e8cf57f",
    "cmake/download_externalproject.cmake": "cb384f2032ea35ef487fdae7e32bdcdec9b48464df0ea8c54b3ac3c0b7ea9f88",
}
OVERLAYS = {
    "toolchain/rustup.cmake": "rustup.cmake",
    "cmake/download_externalproject.cmake": "download_externalproject.cmake",
}
OVERLAY_SHA256 = {
    "rustup.cmake": "c9d159fbc8902f5e38bb61672d8771ce67ce99abe07e8ecccebd006e6b26aac2",
    "download_externalproject.cmake": "9451eab45f0d77d4fc84c324fa36e4370b75437d409876bdd71aface676cb0b3",
}
REQUIRED_COMPONENTS = {
    "externalproject_module", "rustc", "cargo", "windows_std_manifest",
}


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def checked_path(root, relative, *, directory=False):
    """Resolve a contained path while rejecting links and ambiguous spellings."""
    if not isinstance(relative, str) or not relative or "\\" in relative:
        raise ValueError("component path must be a nonempty slash-separated string")
    parts = relative.split("/")
    if any(part in ("", ".", "..") or ":" in part for part in parts):
        raise ValueError("ambiguous or absolute component path")
    root = Path(root).absolute()
    for candidate in (root, *root.parents):
        if candidate.is_symlink() or getattr(candidate.lstat(), "st_file_attributes", 0) & 0x400:
            raise ValueError("linked or reparse-point root")
    path = root
    for part in parts:
        path = path / part
        info = path.lstat()
        if path.is_symlink() or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError("linked or reparse-point component")
    if directory != path.is_dir() or (not directory and not path.is_file()):
        raise ValueError("unexpected component type")
    if os.path.commonpath((str(root), str(path.absolute()))) != str(root):
        raise ValueError("component escaped its root")
    return path


def verify_file(root, record):
    if not isinstance(record, dict) or set(record) != {"path", "sha256", "size"}:
        raise ValueError("invalid component record")
    digest = record["sha256"]
    if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
        raise ValueError("invalid SHA-256")
    if type(record["size"]) is not int or record["size"] < 1:
        raise ValueError("invalid component size")
    payload = checked_path(root, record["path"]).read_bytes()
    if len(payload) != record["size"] or sha256_bytes(payload) != digest:
        raise ValueError("component bytes differ from lock")
    return payload


def plan(provider_root, prestage_root, lock, overlay_root):
    """Verify inputs and return exactly two immutable replacement byte strings."""
    if not isinstance(lock, dict) or set(lock) != {"provider", "rust", "components"}:
        raise ValueError("unsupported lock fields")
    if lock["provider"] != PROVIDER or lock["rust"] != "nightly-2026-10-11":
        raise ValueError("unexpected provider or Rust selection")
    components = lock["components"]
    if not isinstance(components, dict) or set(components) != REQUIRED_COMPONENTS:
        raise ValueError("unresolved or unknown component IDs")
    paths = [item.get("path") for item in components.values() if isinstance(item, dict)]
    if len(paths) != len(REQUIRED_COMPONENTS) or len(set(paths)) != len(paths):
        raise ValueError("ambiguous component paths")
    for item in components.values():
        verify_file(prestage_root, item)
    rustc = components["rustc"]["path"]
    cargo = components["cargo"]["path"]
    std = components["windows_std_manifest"]["path"]
    if not rustc.endswith("/bin/rustc") or cargo != rustc[:-5] + "cargo":
        raise ValueError("unexpected Rust host layout")
    if std != rustc[:-len("bin/rustc")] + "lib/rustlib/x86_64-pc-windows-gnu/manifest-rust-std-x86_64-pc-windows-gnu":
        raise ValueError("unexpected Rust target layout")
    replacements = {}
    for original, expected in ORIGINALS.items():
        payload = checked_path(provider_root, original).read_bytes()
        if sha256_bytes(payload) != expected:
            raise ValueError("provider source bytes differ: " + original)
        overlay = OVERLAYS[original]
        replacement = checked_path(overlay_root, overlay).read_bytes()
        if sha256_bytes(replacement) != OVERLAY_SHA256[overlay]:
            raise ValueError("overlay bytes differ: " + overlay)
        replacements[original] = replacement
    for original in (*recipe_edits.MESON, "packages/subrandr.cmake"):
        payload = checked_path(provider_root, original).read_bytes()
        replacements[original] = recipe_edits.edit(original, payload)
    return replacements


def apply(provider_root, prestage_root, lock, overlay_root, destination):
    """Create a new owned copy with verified whole-file replacements."""
    replacements = plan(provider_root, prestage_root, lock, overlay_root)
    source = Path(provider_root).absolute()
    destination = Path(destination).absolute()
    if destination.exists() or destination.is_symlink():
        raise ValueError("destination already exists")
    parent = destination.parent
    if parent.resolve() != parent.absolute() or not parent.is_dir():
        raise ValueError("destination parent must be an existing unlinked directory")
    for candidate in (parent, *parent.parents):
        if candidate.is_symlink() or getattr(candidate.lstat(), "st_file_attributes", 0) & 0x400:
            raise ValueError("linked or reparse-point destination")
    if os.path.commonpath((str(source), str(destination))) == str(source):
        raise ValueError("destination must not be inside provider")
    # copytree rejects existing destinations; symlink traversal is prohibited.
    for base, dirs, files in os.walk(source, followlinks=False):
        for name in dirs + files:
            entry = Path(base) / name
            info = entry.lstat()
            if entry.is_symlink() or getattr(info, "st_file_attributes", 0) & 0x400:
                raise ValueError("provider contains link or reparse point")
    shutil.copytree(source, destination, symlinks=False)
    for relative, payload in replacements.items():
        target = checked_path(destination, relative)
        expected = ORIGINALS.get(relative, recipe_edits.MESON.get(relative, recipe_edits.SUBRANDR))
        if sha256_bytes(target.read_bytes()) != expected:
            raise ValueError("copied provider source changed: " + relative)
        target.write_bytes(payload)
        if target.read_bytes() != payload:
            raise OSError("replacement verification failed")
    return {name: sha256_bytes(data) for name, data in replacements.items()}
