"""Process-local error-dialog guard for explicitly invoked native qualification."""
import ctypes
from contextlib import contextmanager
import json
import os
from pathlib import Path

SEM_FAILCRITICALERRORS = 0x0001
SEM_NOGPFAULTERRORBOX = 0x0002
REQUESTED = SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX


class NativeErrorMode:
    def __init__(self):
        if os.name != 'nt':
            raise OSError('Windows is required for native error mode')
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.GetErrorMode.argtypes = []
        kernel.GetErrorMode.restype = ctypes.c_uint
        kernel.SetErrorMode.argtypes = [ctypes.c_uint]
        kernel.SetErrorMode.restype = ctypes.c_uint
        self.kernel = kernel

    def get_error_mode(self):
        return int(self.kernel.GetErrorMode())

    def set_error_mode(self, value):
        return int(self.kernel.SetErrorMode(value))


def _write_evidence(path, record):
    payload = (json.dumps(record, sort_keys=True, indent=2) + '\n').encode('utf-8')
    with open(path, 'xb') as stream:
        if stream.write(payload) != len(payload):
            raise OSError('incomplete error-mode evidence write')
        stream.flush()
        os.fsync(stream.fileno())


@contextmanager
def quiet_error_ui(evidence_root, api=None):
    """Suppress specified process error dialogs for the body, then restore the prior mode."""
    root = Path(evidence_root)
    if not root.is_absolute() or not root.is_dir():
        raise ValueError('an existing absolute evidence directory is required')
    backend = NativeErrorMode() if api is None else api
    previous = backend.get_error_mode()
    restore_to = previous
    completed = False
    try:
        returned = backend.set_error_mode(previous | REQUESTED)
        restore_to = returned
        if returned != previous:
            raise OSError('process error mode changed during setup')
        effective = backend.get_error_mode()
        if effective & (previous | REQUESTED) != previous | REQUESTED:
            raise OSError('process error mode verification failed')
        _write_evidence(root / 'error-ui-start.json', {
            'previous_mask': previous, 'effective_mask': effective, 'requested_bits': REQUESTED,
            'wer_fault_dialog_suppression_requested': True,
        })
        yield
        completed = True
    finally:
        backend.set_error_mode(restore_to)
        restored = backend.get_error_mode()
        if restored != restore_to:
            raise OSError('process error mode restoration failed')
        if completed:
            _write_evidence(root / 'error-ui-restored.json', {'restored_mask': restored})
