"""Fail-closed normalization of the pinned Linux Qt pkg-config metadata."""

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess


VERSION = "qt-pc-prefix-v1"
OLD = b"prefix=/home/qt/work/install"
BAKED = b"/home/qt/work/install"
NEW = b"prefix=${pcfiledir}/../.."
MARKER = "qt-pc-normalization.started"
MANIFEST = "qt-pc-normalization.json"
ZLIB_EVIDENCE = "qt-host-zlib.json"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def validate_pkg_environment(prefix=None):
    allowed = str(Path(prefix) / "lib/pkgconfig") if prefix is not None else None
    for key, value in os.environ.items():
        if (key == "PKG_CONFIG" or key.startswith("PKG_CONFIG_")) and value:
            if key != "PKG_CONFIG_PATH" or value != allowed:
                raise ValueError("conflicting pkg-config environment override")
    selected = shutil.which("pkg-config")
    if not selected or Path(selected).resolve() != Path("/usr/bin/pkg-config").resolve():
        raise ValueError("default system pkg-config is not selected")


def host_zlib(prefix=None, include_candidate=True):
    validate_pkg_environment(prefix)
    environment = os.environ.copy()
    environment.pop("PKG_CONFIG_PATH", None)
    if prefix is not None and include_candidate:
        environment["PKG_CONFIG_PATH"] = str(Path(prefix) / "lib/pkgconfig")
    result = {}
    for option in ("--modversion", "--variable=pcfiledir", "--variable=prefix", "--variable=includedir",
                   "--variable=libdir", "--cflags", "--libs"):
        command = subprocess.run(["/usr/bin/pkg-config", option, "zlib"], env=environment, check=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=15)
        result[option] = command.stdout.strip()
    if not result["--variable=pcfiledir"] or not result["--variable=prefix"]:
        raise ValueError("host zlib metadata identity missing")
    return result


def load_pins():
    value = json.loads(Path(__file__).with_name("qt-pc-prefix-v1.json").read_text(encoding="utf-8"))
    files = value.get("files")
    if value.get("version") != VERSION or not isinstance(files, list) or len(files) != 70:
        raise ValueError("invalid pinned Qt metadata inventory")
    names = set()
    for item in files:
        name = item.get("name")
        if (not isinstance(name, str) or not re.fullmatch(r"Qt6[A-Za-z0-9_]+\.pc", name) or
                name in names or item.get("mode") != 0o644 or type(item.get("bytes")) is not int or
                not 0 < item["bytes"] <= 65536 or
                any(not re.fullmatch(r"[0-9a-f]{64}", item.get(key, ""))
                    for key in ("sha256", "after_sha256"))):
            raise ValueError("invalid pinned Qt metadata entry")
        names.add(name)
    return files


def checked_directory(prefix):
    prefix = Path(prefix)
    if not prefix.is_absolute() or prefix.resolve(strict=True) != prefix:
        raise ValueError("Qt candidate must be a canonical absolute path")
    directory = prefix
    for component in ("lib", "pkgconfig"):
        directory = directory / component
        info = directory.lstat()
        if not stat.S_ISDIR(info.st_mode) or directory.resolve(strict=True) != directory:
            raise ValueError("Qt pkg-config directory is not a contained real directory")
    return directory


def checked_read(path, mode, size):
    flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
    original = path.lstat()
    if not stat.S_ISREG(original.st_mode):
        raise ValueError(f"non-regular Qt metadata file: {path.name}")
    fd = os.open(path, flags)
    try:
        info = os.fstat(fd)
        if (not stat.S_ISREG(info.st_mode) or stat.S_IMODE(info.st_mode) != mode or
                info.st_nlink != 1 or info.st_size != size or size > 65536 or
                (info.st_dev, info.st_ino) != (original.st_dev, original.st_ino)):
            raise ValueError(f"unexpected Qt metadata file identity: {path.name}")
        with os.fdopen(fd, "rb", closefd=False) as source:
            data = source.read(size + 1)
        if len(data) != size:
            raise ValueError(f"Qt metadata file size changed: {path.name}")
        return data
    finally:
        os.close(fd)


def transformed(data):
    if (any(byte < 32 and byte not in (9, 10, 13) or byte == 127 for byte in data) or
            b"\\\n" in data or b"\\\r\n" in data or
            b"\r" in data.replace(b"\r\n", b"")):
        raise ValueError("ambiguous Qt metadata line syntax")
    lines = data.splitlines(keepends=True)
    prefixes = [line.rstrip(b"\r\n") for line in lines if re.match(rb"[ \t]*prefix[ \t]*=", line)]
    if prefixes != [OLD] or data.count(BAKED) != 1:
        raise ValueError("unexpected or repeated Qt metadata prefix")
    return b"".join(NEW + line[len(OLD):] if line.rstrip(b"\r\n") == OLD else line for line in lines)


def check_set(directory, pins):
    actual = {path.name for path in directory.iterdir()}
    expected = {item["name"] for item in pins}
    if actual != expected:
        raise ValueError("Qt metadata file set differs from pinned inventory")


def checked_workspace(prefix, workspace):
    workspace = Path(workspace)
    if not workspace.is_absolute() or workspace.resolve(strict=True) != workspace or not workspace.is_dir():
        raise ValueError("Qt workspace must be canonical")
    if hasattr(os, "getuid") and workspace.stat().st_uid != os.getuid():
        raise ValueError("Qt workspace is not owned by this user")
    if Path(prefix) != workspace / "qt-candidate" / "6.9.3" / "gcc_64":
        raise ValueError("Qt candidate is outside its workspace")
    return workspace


def preflight(directory, pins):
    check_set(directory, pins)
    planned = []
    for item in pins:
        data = checked_read(directory / item["name"], item["mode"], item["bytes"])
        if digest(data) != item["sha256"]:
            raise ValueError(f"Qt metadata SHA-256 mismatch: {item['name']}")
        output = transformed(data)
        if digest(output) != item["after_sha256"]:
            raise ValueError(f"Qt metadata transformation mismatch: {item['name']}")
        planned.append((item, data, output))
    return planned


def write_exclusive(path, content, mode=0o600):
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    with os.fdopen(os.open(path, flags, mode), "wb") as output:
        output.write(content)
        output.flush()
        os.fsync(output.fileno())


def read_evidence(path, maximum):
    info = path.lstat()
    if (not stat.S_ISREG(info.st_mode) or info.st_size > maximum or info.st_nlink != 1 or
            (hasattr(os, "getuid") and (info.st_uid != os.getuid() or info.st_mode & 0o022))):
        raise ValueError(f"invalid Qt normalization evidence: {path.name}")
    return checked_read(path, stat.S_IMODE(info.st_mode), info.st_size)


def normalize_qt_pc(prefix, workspace):
    workspace = checked_workspace(prefix, workspace)
    directory, pins = checked_directory(prefix), load_pins()
    marker, manifest = workspace / MARKER, workspace / MANIFEST
    ready = workspace / "qt-sdk-ready.json"
    if any(path.exists() or path.is_symlink() for path in (marker, manifest, ready)):
        raise ValueError("Qt metadata normalization already attempted")
    planned = preflight(directory, pins)
    write_exclusive(marker, b"incomplete until verified\n")
    records = []
    for item, data, output in planned:
        target = directory / item["name"]
        if checked_read(target, item["mode"], item["bytes"]) != data:
            raise ValueError(f"Qt metadata changed during normalization: {item['name']}")
        temporary = directory / ("." + item["name"] + ".qt-pc-pending")
        write_exclusive(temporary, output, item["mode"])
        temporary.chmod(item["mode"])
        os.replace(temporary, target)
        actual = checked_read(target, item["mode"], len(output))
        if actual != output:
            raise ValueError(f"Qt metadata write verification failed: {item['name']}")
        records.append({"name": item["name"], "before_mode": item["mode"], "after_mode": item["mode"],
                        "before_bytes": len(data), "after_bytes": len(actual),
                        "before_sha256": digest(data), "after_sha256": digest(actual)})
    payload = json.dumps({"version": VERSION, "files": records}, sort_keys=True).encode("utf-8") + b"\n"
    write_exclusive(manifest, payload)
    verify_normalized(prefix, workspace)
    return digest(payload)


def verify_normalized(prefix, workspace):
    workspace = checked_workspace(prefix, workspace)
    directory, pins = checked_directory(prefix), load_pins()
    if read_evidence(workspace / MARKER, 1024) != b"incomplete until verified\n":
        raise ValueError("invalid Qt metadata normalization marker")
    payload = read_evidence(workspace / MANIFEST, 1024 * 1024)
    value = json.loads(payload)
    entries = value.get("files")
    if value.get("version") != VERSION or not isinstance(entries, list) or len(entries) != len(pins):
        raise ValueError("invalid Qt metadata normalization evidence")
    check_set(directory, pins)
    for pin, entry in zip(pins, entries, strict=True):
        expected = {"name": pin["name"], "before_mode": pin["mode"], "after_mode": pin["mode"],
                    "before_bytes": pin["bytes"],
                    "after_bytes": pin["bytes"] - len(OLD) + len(NEW),
                    "before_sha256": pin["sha256"], "after_sha256": pin["after_sha256"]}
        if entry != expected:
            raise ValueError("Qt metadata normalization evidence differs from pins")
        data = checked_read(directory / pin["name"], pin["mode"], expected["after_bytes"])
        if digest(data) != pin["after_sha256"]:
            raise ValueError(f"normalized Qt metadata changed: {pin['name']}")
    return {"version": VERSION, "count": len(entries), "sha256": digest(payload)}
