"""Select nine approved text members from three pinned local sdists; no extraction."""

import argparse
from contextlib import ExitStack
import gzip
import hashlib
import json
import os
from pathlib import Path
import stat

import macos_sdist_static as bounded

ARCHIVES = {
    "cython-3.3.1.tar.gz": {
        "size": 3743354,
        "sha256": "e0e45989e821bef6aa9ae15c7d583ab5e055f7406c4b3624e49e06d24e90375a",
        "members": ["Cython/Distutils/build_ext.py", "Cython/__init__.py", "Cython/Compiler/Main.py",
                    "Cython/Build/Dependencies.py", "Cython/Parser/Grammar"],
        "root": "cython-3.3.1",
    },
    "packaging-26.3.tar.gz": {
        "size": 313412,
        "sha256": "94edc256424af38762eb31306eed28beb9f0efc50a8837492c9d6fd6004aed79",
        "members": ["src/packaging/__init__.py", "src/packaging/utils.py", "src/packaging/version.py"],
        "root": "packaging-26.3",
    },
    "pyproject_metadata-0.12.1.tar.gz": {
        "size": 48457,
        "sha256": "8809a4df6fe08279b39a8890669506ed3158e0617855ac9aff098fcbe772ae4c",
        "members": ["pyproject_metadata/__init__.py"],
        "root": "pyproject_metadata-0.12.1",
    },
}


def validate_directory(value):
    path = Path(value).absolute()
    if not path.is_dir():
        raise ValueError("source directory is unavailable")
    for part in (path, *path.parents):
        info = part.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
            raise ValueError("link or reparse point in source ancestry")
    return path


def hash_stream(stream):
    digest = hashlib.sha256()
    stream.seek(0)
    while chunk := stream.read(65536):
        digest.update(chunk)
    return digest.hexdigest()


def open_verified(stack, directory, name, spec):
    path = directory / name
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
        raise ValueError("archive is not a regular file")
    stream = stack.enter_context(path.open("rb"))
    opened = os.fstat(stream.fileno())
    if (info.st_dev, info.st_ino) != (opened.st_dev, opened.st_ino):
        raise ValueError("archive identity changed while opening")
    if opened.st_size != spec["size"] or hash_stream(stream) != spec["sha256"]:
        raise ValueError("archive size or hash mismatch")
    return stream, opened


def inspect_one(stream, spec, expanded, result_size):
    requested = {spec["root"] + "/" + member for member in spec["members"]}
    original = bounded.candidate
    bounded.candidate = lambda path: str(path) in requested
    try:
        stream.seek(0)
        with gzip.GzipFile(fileobj=stream, mode="rb") as gz:
            reader = bounded.CountedReader(gz, expanded)
            total, found, result_size = bounded.inspect_stream(reader, {}, [], result_size)
    finally:
        bounded.candidate = original
    if set(found["text"]) != requested:
        raise ValueError("requested member missing or not a regular file")
    return total, found["text"], result_size


def select(directory):
    sources = {}
    records = []
    expanded = result_size = 0
    with ExitStack() as stack:
        for name, spec in ARCHIVES.items():
            sources[name] = open_verified(stack, directory, name, spec)
        for name, spec in ARCHIVES.items():
            stream, before = sources[name]
            expanded, texts, result_size = inspect_one(stream, spec, expanded, result_size)
            after = os.fstat(stream.fileno())
            if ((before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) !=
                    (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns) or
                    hash_stream(stream) != spec["sha256"]):
                raise ValueError("archive changed during inspection")
            for member in spec["members"]:
                full = spec["root"] + "/" + member
                text = texts[full]
                raw = text.encode("utf-8")
                records.append({"archive": name, "archive_bytes": spec["size"],
                                "archive_sha256": spec["sha256"], "member": full,
                                "bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest(), "text": text})
    if len(records) != 9:
        raise ValueError("unexpected selection count")
    return {"expanded": expanded, "members": records}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", required=True)
    args = parser.parse_args()
    result = json.dumps(select(validate_directory(args.source_dir)), ensure_ascii=True, indent=2)
    if len(result.encode("utf-8")) > bounded.RESULT_LIMIT:
        raise ValueError("serialized result exceeds cap")
    print(result)


if __name__ == "__main__":
    main()
