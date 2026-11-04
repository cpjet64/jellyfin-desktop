"""Structured, strict libarchive reader; no extraction is delegated to it."""

import ctypes
import hashlib
import os
import stat

from linux_qt_library import E
from qt_sdk_archive import Entry


class Reader:
    def __init__(self, path, library, expected=None):
        self.path, self.lib = path, library
        self.expected = expected
        self.archive, self.fd = None, None
        self.current, self.done = False, False

    def __enter__(self):
        try:
            self.fd = os.open(self.path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0))
            if self.expected is not None:
                self.verify_fd()
            self.archive = self.lib.archive_read_new()
            if not self.archive:
                raise ValueError("libarchive reader allocation failed")
            self.check(self.lib.archive_read_support_filter_none(self.archive), "filter support")
            self.check(self.lib.archive_read_support_format_7zip(self.archive), "7zip support")
            self.check(self.lib.archive_read_open_fd(self.archive, self.fd, 65536), "archive open")
            return self
        except BaseException:
            self.__exit__(*__import__("sys").exc_info())
            raise

    @staticmethod
    def check(status, operation):
        if status != 0:
            raise ValueError(f"libarchive {operation} failed: {status}")

    def verify_fd(self):
        info = os.fstat(self.fd)
        if not stat.S_ISREG(info.st_mode) or info.st_size != self.expected["bytes"]:
            raise ValueError("archive file type or size mismatch")
        digest = hashlib.sha256()
        os.lseek(self.fd, 0, os.SEEK_SET)
        while chunk := os.read(self.fd, 1024 * 1024):
            digest.update(chunk)
        if digest.hexdigest() != self.expected["sha256"]:
            raise ValueError("archive SHA-256 mismatch")
        os.lseek(self.fd, 0, os.SEEK_SET)

    def __exit__(self, exc_type, exc, traceback):
        errors = []
        if self.archive:
            try:
                self.check(self.lib.archive_read_free(self.archive), "reader cleanup")
            except BaseException as error:
                errors.append(error)
            self.archive = None
        if self.fd is not None:
            try:
                os.close(self.fd)
            except BaseException as error:
                errors.append(error)
            self.fd = None
        if errors and exc is None:
            raise errors[0]
        return False

    def next_entry(self):
        if self.done:
            raise ValueError("archive traversal already completed")
        if self.current:
            self.check(self.lib.archive_read_data_skip(self.archive), "data skip")
        pointer = E()
        status = self.lib.archive_read_next_header(self.archive, ctypes.byref(pointer))
        self.current = False
        if status == 1:
            if self.lib.archive_read_has_encrypted_entries(self.archive) != 0:
                raise ValueError("archive encryption status is not known unencrypted")
            if self.expected is not None:
                self.verify_fd()
            self.done = True
            return None
        self.check(status, "header")
        if not pointer or self.lib.archive_format(self.archive) & 0xff0000 != 0xE0000:
            raise ValueError("archive is not 7zip")
        if self.lib.archive_filter_code(self.archive, 0) != 0:
            raise ValueError("outer archive filter rejected")
        entry = self.metadata(pointer)
        self.current = True
        return entry

    def metadata(self, pointer):
        lib = self.lib
        name = lib.archive_entry_pathname(pointer)
        if not name or lib.archive_entry_hardlink(pointer):
            raise ValueError("missing pathname or hard link rejected")
        if (lib.archive_entry_is_encrypted(pointer) > 0 or lib.archive_entry_fflags_text(pointer) or
                lib.archive_entry_acl_types(pointer) or
                lib.archive_entry_xattr_count(pointer) or lib.archive_entry_sparse_count(pointer)):
            raise ValueError("encrypted, ACL, xattr or sparse entry rejected")
        mode = lib.archive_entry_mode(pointer)
        kind = {stat.S_IFREG: "file", stat.S_IFDIR: "directory", stat.S_IFLNK: "symlink"}.get(stat.S_IFMT(mode))
        if kind is None or mode & 0o7000:
            raise ValueError("special entry or privileged mode rejected")
        if not lib.archive_entry_size_is_set(pointer):
            raise ValueError("entry size unknown")
        size = lib.archive_entry_size(pointer)
        if size < 0:
            raise ValueError("negative entry size")
        link = lib.archive_entry_symlink(pointer)
        if kind == "symlink" and not link:
            raise ValueError("symlink target missing")
        if kind != "symlink" and link:
            raise ValueError("unexpected symlink target")
        return Entry(name.decode("utf-8", "strict"), kind, size, mode & 0o777,
                     link.decode("utf-8", "strict") if link else "")

    def read(self, size):
        if not self.current or type(size) is not int or size <= 0:
            raise ValueError("positive read size and current entry required")
        buffer = ctypes.create_string_buffer(size)
        count = self.lib.archive_read_data(self.archive, buffer, size)
        if count < 0 or count > size:
            raise ValueError(f"libarchive data read failed: {count}")
        return buffer.raw[:count]
