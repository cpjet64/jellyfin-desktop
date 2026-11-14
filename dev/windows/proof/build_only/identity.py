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


def _annotate(error, **values):
    fields = ('operation', 'process_creation_time', 'process_image_path', 'input_path',
              'final_path', 'file_id', 'observed_size', 'expected_size')
    observation = {key: None for key in fields}
    observation.update(getattr(error, 'identity_observation', {}))
    observation.update(values)
    error.identity_observation = observation


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
        except BaseException as error:
            context = {'operation': 'pin_open', 'input_path': self.path,
                       'final_path': getattr(self, 'final_path', None), 'file_id': getattr(self, 'file_id', None),
                       'observed_size': getattr(self, 'size', None), 'expected_size': self.expected_size}
            _annotate(error, **context)
            try:
                self.close()
            except BaseException as close_error:
                _annotate(close_error, **context)
                raise close_error from error
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
        try:
            with FilePin(self.api, self.path, self.expected_sha256, self.expected_size) as current:
                if current.file_id != self.file_id or current.final_path.casefold() != self.final_path.casefold():
                    raise OSError('pinned file path identity changed')
        except BaseException as error:
            _annotate(error, operation='pinned_file_recheck')
            raise

    def verify_process(self, api, process, job):
        created, image, operation = None, None, 'process_validation'
        reported_details = None
        try:
            if api is not self.api or api.wait(process, 0) or not api.in_job(process, job):
                raise OSError('process is not a live member of the owned job')
            created = api.creation_time(process)
            image = api.image_path(process)
            operation = 'process_reported_image'
            with FilePin(api, image, self.expected_sha256, self.expected_size) as reported:
                reported_details = {'input_path': reported.path, 'final_path': reported.final_path,
                                    'file_id': reported.file_id, 'observed_size': reported.size,
                                    'expected_size': self.expected_size}
                if reported.file_id != self.file_id or reported.final_path != self.final_path:
                    raise OSError('process image file identity mismatch')
            operation = 'pinned_file_recheck'
            self.assert_path_unchanged()
            operation = 'process_validation'
            if api.wait(process, 0) or not api.in_job(process, job) or api.creation_time(process) != created:
                raise OSError('process identity or membership changed')
        except BaseException as error:
            if operation == 'process_reported_image' and reported_details is not None:
                _annotate(error, **reported_details)
            elif operation == 'process_reported_image' and not getattr(error, 'identity_observation', {}).get('input_path'):
                _annotate(error, input_path=image, expected_size=self.expected_size)
            _annotate(error, operation=operation, process_creation_time=created, process_image_path=image)
            raise
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


class ObservedFile:
    """Observe a known build output after settlement; this does not qualify an input."""
    _open_chain = FilePin._open_chain
    _metadata = FilePin._metadata
    _hash = FilePin._hash
    close = FilePin.close
    __enter__ = FilePin.__enter__
    __exit__ = FilePin.__exit__

    def __init__(self, api, absolute_path):
        if os.name != 'nt':
            raise OSError('Windows is required')
        if not os.path.isabs(absolute_path):
            raise ValueError('absolute output path required')
        self.api, self.path = api, os.path.abspath(absolute_path)
        self.handles = []
        try:
            self._open_chain()
            self.handle = self.handles[-1]
            self.final_path, self.file_id, self.size = self._metadata(self.handle)
            final = self.final_path
            if final.startswith('\\\\?\\UNC\\'):
                final = '\\\\' + final[8:]
            elif final.startswith('\\\\?\\'):
                final = final[4:]
            if os.path.normcase(os.path.normpath(final)) != os.path.normcase(self.path):
                raise OSError('output final path mismatch')
            self.sha256 = self._hash(self.handle)
        except BaseException:
            self.close()
            raise
