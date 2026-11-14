"""Exclusive, durably flushed receipt, independent of the build handle ledger."""
import ctypes as C
import os
from pathlib import Path

from . import winapi as w

GENERIC_WRITE = 0x40000000
CREATE_NEW = 1


class ReceiptCloseError(OSError):
    """The caller retains the exact handles whose close failed."""
    def __init__(self, handles):
        super().__init__('receipt handle close failed')
        self.retained_handles = tuple(handles)


class ReceiptFile:
    def __init__(self, path):
        if os.name != 'nt':
            raise OSError('Windows is required')
        if not os.path.isabs(path) or not os.path.basename(path):
            raise ValueError('absolute receipt file path required')
        self.api = w.WinAPI()
        self.path = os.path.abspath(path)
        self.handles = []
        self.file = None
        self.failed = False
        try:
            self._open_parents()
            raw = self.api.k.CreateFileW(self.path, GENERIC_WRITE, 0, None, CREATE_NEW, 0, None)
            self.file = w.Handle(self.api, raw)
            self.handles.append(self.file)
        except BaseException:
            self._close_handles()
            raise

    def _open_parents(self):
        parent = Path(self.path).parent
        for part in list(reversed(parent.parents)) + [parent]:
            flags = w.FILE_FLAG_OPEN_REPARSE_POINT | w.FILE_FLAG_BACKUP_SEMANTICS
            raw = self.api.k.CreateFileW(str(part), w.FILE_READ_ATTRIBUTES, w.FILE_SHARE_READ,
                                         None, w.OPEN_EXISTING, flags, None)
            handle = w.Handle(self.api, raw)
            self.handles.append(handle)
            info = w.FILE_INFO()
            self.api.check(self.api.k.GetFileInformationByHandle(handle.value, C.byref(info)))
            if info.attributes & w.FILE_ATTRIBUTE_REPARSE_POINT:
                raise OSError('reparse point in receipt parent path')
            if not info.attributes & 0x10:
                raise OSError('receipt parent is not a directory')

    def write(self, data):
        if self.failed or self.file is None or self.file.value is None:
            raise OSError('receipt is not writable')
        if not isinstance(data, bytes):
            self.failed = True
            raise TypeError('receipt data must be bytes')
        offset = 0
        try:
            while offset < len(data):
                part = data[offset:offset + 65536]
                buffer = C.create_string_buffer(part)
                written = w.DWORD()
                self.api.check(self.api.k.WriteFile(self.file.value, buffer, len(part), C.byref(written), None))
                if not written.value or written.value > len(part):
                    raise OSError('receipt write made invalid progress')
                offset += written.value
        except BaseException:
            self.failed = True
            raise

    def flush(self):
        if self.failed or self.file is None or self.file.value is None:
            raise OSError('receipt is not flushable')
        try:
            self.api.check(self.api.k.FlushFileBuffers(self.file.value))
        except BaseException:
            self.failed = True
            raise

    def _close_handles(self):
        failed = []
        for handle in reversed(self.handles):
            try:
                handle.close()
            except OSError:
                failed.append(handle)
        self.handles = list(reversed(failed))
        if failed:
            self.failed = True
            raise ReceiptCloseError(self.handles)

    def close(self):
        if not self.handles:
            if self.failed:
                raise OSError('receipt previously failed')
            return
        error = None
        if not self.failed and self.file is not None and self.file.value is not None:
            try:
                self.flush()
            except OSError as failure:
                error = failure
        self._close_handles()
        if error:
            raise error
        if self.failed:
            raise OSError('receipt previously failed')

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
