"""Validate the approved Linux split SDK before any libmpv consumer runs."""
import hashlib
import io
from pathlib import Path
import subprocess
import tarfile

SHA = "c9cfd0b2f60e6377f0d339ef298a0772753ce7dc7310d250573a3ea9c0b9701e"
VERSION = "0.37.0-1ubuntu4"
KEYS = ("CI_MPV_DEV_DEB", "CI_MPV_INCLUDE_DIR", "CI_MPV_LIBRARY")


def explicit_inputs(environment, system, root):
    values = [environment.get(key, "") for key in KEYS]
    if not any(values):
        return None
    if system != "Linux" or not all(values):
        raise RuntimeError("BLOCKED: explicit mpv route requires all three Linux inputs")
    archive, include, library = (Path(value) for value in values)
    if not all(path.is_absolute() for path in (archive, include, library)):
        raise RuntimeError("BLOCKED: explicit mpv paths must be absolute")
    try:
        archive, include, library = (path.resolve(strict=True) for path in (archive, include, library))
        archive.relative_to(root.resolve(strict=True))
        include.relative_to(root.resolve(strict=True))
    except (OSError, ValueError) as error:
        raise RuntimeError("BLOCKED: mpv archive and headers must be job-owned and all paths exist") from error
    if not archive.is_file() or not include.is_dir() or not library.is_file():
        raise RuntimeError("BLOCKED: invalid explicit mpv files")
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA:
        raise RuntimeError("BLOCKED: mpv development package SHA mismatch")
    verify_headers(archive, include)
    verify_library(library)
    return include, library


def verify_headers(archive, include):
    result = subprocess.run(["dpkg-deb", "--fsys-tarfile", str(archive)], check=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    with tarfile.open(fileobj=io.BytesIO(result.stdout), mode="r:") as contents:
        members = {item.name.removeprefix("./"): item for item in contents if item.isfile()}
        for name in ("client.h", "render.h", "render_gl.h", "stream_cb.h"):
            member = members.get("usr/include/mpv/" + name)
            target = include / "mpv" / name
            if not member or not target.is_file() or target.is_symlink():
                raise RuntimeError("BLOCKED: missing extracted mpv header " + name)
            try:
                target.resolve(strict=True).relative_to(include)
            except (OSError, ValueError) as error:
                raise RuntimeError("BLOCKED: mpv header escapes explicit include directory") from error
            if contents.extractfile(member).read() != target.read_bytes():
                raise RuntimeError("BLOCKED: extracted mpv header differs from verified package: " + name)


def verify_library(library):
    owner = subprocess.check_output(["dpkg-query", "-S", str(library)], text=True, timeout=15).strip()
    package = owner.split(": ", 1)[0]
    if package not in ("libmpv2", "libmpv2:amd64"):
        raise RuntimeError("BLOCKED: mpv runtime is not owned by libmpv2")
    version = subprocess.check_output(["dpkg-query", "-W", "-f=${Version}", package], text=True, timeout=15).strip()
    if version != VERSION:
        raise RuntimeError("BLOCKED: mpv runtime package version mismatch")


def require_no_pkgconfig_mpv(program):
    result = subprocess.run([program, "--exists", "mpv"], stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, timeout=15)
    if result.returncode != 1:
        raise RuntimeError("BLOCKED: explicit mpv route requires absent pkg-config mpv metadata")


def cmake_options(paths):
    include, library = paths
    return ["-DMPV_INCLUDE_DIR=" + str(include), "-DMPV_LIBRARY_mpv=" + str(library),
            "-DLibmpv_INCLUDE_DIRS=" + str(include / "mpv"), "-DLibmpv_LIBRARIES=" + str(library)]


def verify_cache(cache_path, paths, mode="system"):
    include, library = paths
    entries = {}
    for line in cache_path.read_text(encoding="utf-8").splitlines():
        if ":" in line and "=" in line:
            key, value = line.split("=", 1)
            entries[key.split(":", 1)[0]] = value
    expected_paths = [("MPV_INCLUDE_DIR", include), ("MPV_LIBRARY_mpv", library)]
    if mode == "static":
        expected_paths.extend((("Libmpv_INCLUDE_DIRS", include / "mpv"), ("Libmpv_LIBRARIES", library)))
    for key, expected in expected_paths:
        value = entries.get(key, "")
        if not value or not Path(value).is_absolute() or Path(value).resolve() != expected:
            raise RuntimeError("BLOCKED: configured " + key + " differs from verified mpv input")
