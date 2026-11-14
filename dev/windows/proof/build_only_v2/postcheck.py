"""Independent, exclusive Windows receipt and output readback for v2."""
import ctypes as C
import hashlib
import ntpath
import re
import sys

from build_only import winapi as w
from .receipt import validate_receipt

MAX_RECEIPT = 1024 * 1024
MAX_OUTPUT = 1024 * 1024 * 1024


class RetainedHandlesError(OSError):
    def __init__(self, handles):
        super().__init__("exclusive postcheck handle closure failed")
        self.retained_handles = tuple(handles)


def _open(api, path):
    raw = api.k.CreateFileW(path, w.GENERIC_READ, 0, None, w.OPEN_EXISTING,
                            w.FILE_FLAG_OPEN_REPARSE_POINT, None)
    handle = w.Handle(api, raw)
    try:
        info = w.FILE_INFO()
        api.check(api.k.GetFileInformationByHandle(handle.value, C.byref(info)))
        if info.attributes & (w.FILE_ATTRIBUTE_REPARSE_POINT | 0x10):
            raise ValueError("unexpected receipt or output file type")
        return handle
    except BaseException as error:
        try:
            handle.close()
        except OSError as close_error:
            raise RetainedHandlesError([handle]) from close_error
        raise error


def _read(api, handle, maximum, retain):
    digest = hashlib.sha256()
    pieces, total = [], 0
    while True:
        buffer, count = C.create_string_buffer(65536), w.DWORD()
        api.check(api.k.ReadFile(handle.value, buffer, len(buffer), C.byref(count), None))
        if not count.value:
            break
        total += count.value
        if total > maximum:
            raise ValueError("file exceeds bounded read")
        data = buffer.raw[:count.value]
        digest.update(data)
        if retain:
            pieces.append(data)
    return (b"".join(pieces) if retain else None), total, digest.hexdigest()


def _outer_exit(raw):
    fields = raw.decode("ascii").splitlines()
    patterns = (r"receipt_version=1", r"root_pid=[1-9][0-9]*", r"root_exit=0",
                r"timeout=0 active=0", r"handles_before=[0-9]+ handles_after=[0-9]+ measured=1",
                r"owned_opened=6 owned_closed=6 close_failed=0", r"handles_complete=1",
                r"final_result=0", r"proof_complete=1")
    if len(fields) != len(patterns) or any(not re.fullmatch(p, v) for p, v in zip(patterns, fields)):
        raise ValueError("outer receipt invalid")
    return int(fields[2].split("=", 1)[1])


def postcheck(*, receipt_path, outer_path, expected_profile, expected_source,
              observed_helper_exit, production=True):
    """Retain exact opened files through validation and reject any failed close."""
    paths = [receipt_path, outer_path, *expected_profile.command_policy.command.outputs]
    if (any(not isinstance(path, str) or not ntpath.isabs(path) for path in paths)
            or len({ntpath.normcase(ntpath.normpath(path)) for path in paths}) != len(paths)):
        raise ValueError("absolute distinct postcheck paths required")
    api = w.WinAPI()
    handles = []
    try:
        outer = _open(api, outer_path)
        handles.append(outer)
        outer_data, _, _ = _read(api, outer, MAX_RECEIPT, True)
        observed_exit = _outer_exit(outer_data)
        if type(observed_helper_exit) is not int or observed_helper_exit != observed_exit:
            raise ValueError("outer helper exit mismatch")
        receipt_api = w.WinAPI()
        receipt = _open(receipt_api, receipt_path)
        try:
            raw, _, _ = _read(receipt_api, receipt, MAX_RECEIPT, True)
        finally:
            try:
                receipt.close()
            except OSError as error:
                raise RetainedHandlesError([receipt]) from error
        if (receipt_api.opened, receipt_api.closed, receipt_api.close_failed) != (1, 1, 0):
            raise OSError("receipt close accounting failed")
        output_evidence = []
        for path in expected_profile.command_policy.command.outputs:
            handle = _open(api, path)
            handles.append(handle)
            _, size, digest = _read(api, handle, MAX_OUTPUT, False)
            output_evidence.append((path, size, digest))
        record = validate_receipt(raw, expected_profile=expected_profile,
                                  expected_source=expected_source, observed_exit=observed_exit,
                                  observed_receipt_closed=True, production=production)
        for item, (path, size, digest) in zip(record["outputs"], output_evidence):
            if item["path"] != path or item["size"] != size or item["sha256"] != digest:
                raise ValueError("output readback mismatch")
    finally:
        pending = sys.exc_info()[1]
        failures = list(getattr(pending, "retained_handles", ()))
        for handle in reversed(handles):
            try:
                handle.close()
            except OSError:
                failures.append(handle)
        if failures or api.opened != api.closed or api.close_failed:
            raise RetainedHandlesError(failures)
    return record
