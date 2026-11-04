"""Verify the specifically approved Ubuntu libarchive before loading it."""

import ctypes
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys


LIBRARY = Path("/usr/lib/x86_64-linux-gnu/libarchive.so.13.7.2")
PACKAGE = "libarchive13t64:amd64"
VERSION = "3.7.2-2ubuntu0.9"
SIZE = 854168
SHA256 = "2d8605282b26359a0994017373bff09fc7c9a6ed9ab7e1a4fd980b8ffcf1cd40"


class Archive(ctypes.Structure):
    pass


class ArchiveEntry(ctypes.Structure):
    pass


A = ctypes.POINTER(Archive)
E = ctypes.POINTER(ArchiveEntry)
P = ctypes.c_void_p
I = ctypes.c_int
S = ctypes.c_size_t
TEXT = ctypes.c_char_p
ABI = {
    "archive_version_number": (I, []),
    "archive_read_new": (A, []),
    "archive_read_support_filter_none": (I, [A]),
    "archive_read_support_format_7zip": (I, [A]),
    "archive_read_open_fd": (I, [A, I, S]),
    "archive_read_next_header": (I, [A, ctypes.POINTER(E)]),
    "archive_read_data": (ctypes.c_ssize_t, [A, P, S]),
    "archive_read_data_skip": (I, [A]),
    "archive_read_has_encrypted_entries": (I, [A]),
    "archive_format": (I, [A]), "archive_filter_code": (I, [A, I]),
    "archive_read_free": (I, [A]),
    "archive_entry_pathname": (TEXT, [E]),
    "archive_entry_hardlink": (TEXT, [E]), "archive_entry_symlink": (TEXT, [E]),
    "archive_entry_fflags_text": (TEXT, [E]),
    "archive_entry_size": (ctypes.c_int64, [E]),
    "archive_entry_size_is_set": (I, [E]),
    "archive_entry_mode": (ctypes.c_uint, [E]),
    "archive_entry_is_encrypted": (I, [E]),
    "archive_entry_acl_types": (I, [E]),
    "archive_entry_xattr_count": (I, [E]),
    "archive_entry_sparse_count": (I, [E]),
}


def query(*args):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, timeout=15).stdout.strip()


def verify_library():
    if LIBRARY.resolve(strict=True) != LIBRARY or not LIBRARY.is_file():
        raise ValueError("approved libarchive path is not a regular, canonical file")
    if query("dpkg-query", "-S", str(LIBRARY)) != f"{PACKAGE}: {LIBRARY}":
        raise ValueError("libarchive package ownership mismatch")
    status = query("dpkg-query", "-W", "-f=${db:Status-Status} ${Version}", PACKAGE)
    if status != f"installed {VERSION}":
        raise ValueError("libarchive package status or version mismatch")
    sums = Path(f"/var/lib/dpkg/info/{PACKAGE}.md5sums").read_text(encoding="utf-8").splitlines()
    match = [line.split("  ", 1)[0] for line in sums if line.endswith("  " + str(LIBRARY)[1:])]
    if len(match) != 1:
        raise ValueError("installed libarchive MD5 metadata missing or ambiguous")
    sha, md5, count, header = hashlib.sha256(), hashlib.md5(usedforsecurity=False), 0, b""
    with LIBRARY.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            if not header:
                header = chunk[:20]
            count += len(chunk)
            sha.update(chunk)
            md5.update(chunk)
    if (count != SIZE or sha.hexdigest() != SHA256 or md5.hexdigest() != match[0] or
            len(header) < 20 or header[:7] != b"\x7fELF\x02\x01\x01" or
            struct.unpack_from("<H", header, 16)[0] != 3 or
            struct.unpack_from("<H", header, 18)[0] != 62):
        raise ValueError("approved libarchive file identity mismatch")


def load_library():
    if sys.platform != "linux":
        raise ValueError("approved libarchive is Linux-only")
    verify_library()
    lib = ctypes.CDLL(os.fspath(LIBRARY))
    for name, (result, arguments) in ABI.items():
        try:
            function = getattr(lib, name)
        except AttributeError as error:
            raise RuntimeError(f"missing libarchive symbol: {name}") from error
        function.restype = result
        function.argtypes = arguments
    if lib.archive_version_number() != 3007002:
        raise ValueError("loaded libarchive version mismatch")
    return lib
