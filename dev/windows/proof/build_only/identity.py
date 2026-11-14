"""Pin on-disk image identity without claiming mapped-image attestation."""
import ctypes as C
import hashlib
import os
from pathlib import Path

from . import winapi as w

VCTIP_PATH = (r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC'
              r'\14.44.35207\bin\Hostx64\x64\vctip.exe')
VCTIP_SHA256 = 'D591358C6AEE4B673562CA3B3A15C250E612708394FE3B44A7A7EDA2470570A5'
VCTIP_SIZE = 335288


class RetainedHandlesError(OSError):
    """Close failed; the caller must retain and resolve these exact handles."""
    def __init__(self, message, handles):
        super().__init__(message)
        self.retained_handles = tuple(handles)


class FilePin:
    def __init__(self, api, path, expected_sha256, expected_size=None):
        if os.name != 'nt':
            raise OSError('Windows is required')
        if not os.path.isabs(path):
            raise ValueError('absolute image path required')
        self.api, self.path = api, os.path.abspath(path)
        self.expected_sha256, self.expected_size = expected_sha256.upper(), expected_size
        self.handles = []
        try:
            self._open_chain()
            self.handle = self.handles[-1]
            self.final_path, self.file_id, self.size = self._metadata(self.handle)
            if expected_size is not None and self.size != expected_size:
                raise OSError('pinned file size mismatch')
            self.sha256 = self._hash(self.handle)
            if self.sha256 != self.expected_sha256:
                raise OSError('pinned file content mismatch')
        except BaseException:
            self.close()
            raise

    def _open_chain(self):
        parts = Path(self.path).parents
        paths = list(reversed(parts)) + [Path(self.path)]
        for index, part in enumerate(paths):
            flags = w.FILE_FLAG_OPEN_REPARSE_POINT
            access = w.FILE_READ_ATTRIBUTES
            if index < len(paths) - 1:
                flags |= w.FILE_FLAG_BACKUP_SEMANTICS
            else:
                access |= w.GENERIC_READ
            raw = self.api.k.CreateFileW(str(part), access, w.FILE_SHARE_READ, None, w.OPEN_EXISTING, flags, None)
            handle = w.Handle(self.api, raw)
            self.handles.append(handle)
            info = w.FILE_INFO()
            self.api.check(self.api.k.GetFileInformationByHandle(handle.value, C.byref(info)))
            if info.attributes & w.FILE_ATTRIBUTE_REPARSE_POINT:
                raise OSError('reparse point in image path')

    def _metadata(self, handle):
        info = w.FILE_INFO()
        self.api.check(self.api.k.GetFileInformationByHandle(handle.value, C.byref(info)))
        buffer = C.create_unicode_buffer(32768)
        length = self.api.k.GetFinalPathNameByHandleW(handle.value, buffer, len(buffer), 0)
        if not length or length >= len(buffer):
            raise OSError('final file path unavailable or truncated')
        identity = (int(info.volume), int(info.index_high), int(info.index_low))
        return buffer.value, identity, (int(info.size_high) << 32) | int(info.size_low)

    def _hash(self, handle):
        digest = hashlib.sha256()
        buffer = C.create_string_buffer(65536)
        while True:
            read = w.DWORD()
            self.api.check(self.api.k.ReadFile(handle.value, buffer, len(buffer), C.byref(read), None))
            if not read.value:
                return digest.hexdigest().upper()
            digest.update(buffer.raw[:read.value])

    def assert_path_unchanged(self):
        with FilePin(self.api, self.path, self.expected_sha256, self.expected_size) as current:
            if current.file_id != self.file_id or current.final_path.casefold() != self.final_path.casefold():
                raise OSError('pinned file path identity changed')

    def verify_process(self, api, process, job):
        if api is not self.api or api.wait(process, 0) or not api.in_job(process, job):
            raise OSError('process is not a live member of the owned job')
        created = api.creation_time(process)
        image = api.image_path(process)
        with FilePin(api, image, self.expected_sha256, self.expected_size) as reported:
            if reported.file_id != self.file_id or reported.final_path != self.final_path:
                raise OSError('process image file identity mismatch')
        self.assert_path_unchanged()
        if api.wait(process, 0) or not api.in_job(process, job) or api.creation_time(process) != created:
            raise OSError('process identity or membership changed')
        return {'path': image, 'creation_time': created, 'volume': self.file_id[0],
                'file_index_high': self.file_id[1], 'file_index_low': self.file_id[2],
                'sha256': self.sha256, 'size': self.size}

    def close(self):
        failed = []
        for handle in reversed(self.handles):
            try:
                handle.close()
            except OSError:
                failed.append(handle)
        self.handles = list(reversed(failed))
        if failed:
            raise RetainedHandlesError('file pin handle close failed', self.handles)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
