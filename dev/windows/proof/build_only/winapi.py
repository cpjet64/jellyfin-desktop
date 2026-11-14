"""Small, checked Win32 boundary for the build-only supervisor.

Imports are portable; constructing WinAPI is intentionally Windows-only.
"""
import ctypes as C
import os
from ctypes import wintypes as W

HANDLE = W.HANDLE
DWORD = W.DWORD
SIZE_T = C.c_size_t
ULONG_PTR = C.c_size_t
INVALID_HANDLE = C.c_void_p(-1).value
WAIT_OBJECT_0, WAIT_TIMEOUT, INFINITE = 0, 258, 0xFFFFFFFF
PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
PROCESS_TERMINATE, SYNCHRONIZE = 1, 0x100000
FILE_READ_ATTRIBUTES, GENERIC_READ = 0x80, 0x80000000
FILE_SHARE_READ = 1
OPEN_EXISTING = 3
FILE_FLAG_OPEN_REPARSE_POINT, FILE_FLAG_BACKUP_SEMANTICS = 0x00200000, 0x02000000
FILE_ATTRIBUTE_REPARSE_POINT = 0x400
CREATE_SUSPENDED, CREATE_NO_WINDOW, EXTENDED_STARTUPINFO_PRESENT = 4, 0x08000000, 0x00080000
STARTF_USESTDHANDLES, DUPLICATE_SAME_ACCESS = 0x100, 2
PROC_THREAD_ATTRIBUTE_HANDLE_LIST = 0x00020002
JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x2000


class BASIC_LIMIT(C.Structure):
    _fields_ = [('PerProcessUserTimeLimit', C.c_int64), ('PerJobUserTimeLimit', C.c_int64),
                ('LimitFlags', DWORD), ('MinimumWorkingSetSize', SIZE_T), ('MaximumWorkingSetSize', SIZE_T),
                ('ActiveProcessLimit', DWORD), ('Affinity', ULONG_PTR), ('PriorityClass', DWORD),
                ('SchedulingClass', DWORD)]


class IO_COUNTERS(C.Structure):
    _fields_ = [(name, C.c_uint64) for name in ('ReadOperationCount', 'WriteOperationCount',
        'OtherOperationCount', 'ReadTransferCount', 'WriteTransferCount', 'OtherTransferCount')]


class EXTENDED_LIMIT(C.Structure):
    _fields_ = [('BasicLimitInformation', BASIC_LIMIT), ('IoInfo', IO_COUNTERS),
                ('ProcessMemoryLimit', SIZE_T), ('JobMemoryLimit', SIZE_T),
                ('PeakProcessMemoryUsed', SIZE_T), ('PeakJobMemoryUsed', SIZE_T)]


class ACCOUNTING(C.Structure):
    _fields_ = [('TotalUserTime', C.c_int64), ('TotalKernelTime', C.c_int64),
                ('ThisPeriodTotalUserTime', C.c_int64), ('ThisPeriodTotalKernelTime', C.c_int64),
                ('TotalPageFaultCount', DWORD), ('TotalProcesses', DWORD),
                ('ActiveProcesses', DWORD), ('TotalTerminatedProcesses', DWORD)]


class FILE_INFO(C.Structure):
    _fields_ = [('attributes', DWORD), ('creation', W.FILETIME), ('access', W.FILETIME),
                ('write', W.FILETIME), ('volume', DWORD), ('size_high', DWORD), ('size_low', DWORD),
                ('links', DWORD), ('index_high', DWORD), ('index_low', DWORD)]


class SECURITY_ATTRIBUTES(C.Structure):
    _fields_ = [('length', DWORD), ('descriptor', C.c_void_p), ('inherit', W.BOOL)]


class STARTUPINFO(C.Structure):
    _fields_ = [('cb', DWORD), ('reserved', W.LPWSTR), ('desktop', W.LPWSTR), ('title', W.LPWSTR),
                ('x', DWORD), ('y', DWORD), ('xsize', DWORD), ('ysize', DWORD), ('xchars', DWORD),
                ('ychars', DWORD), ('fill', DWORD), ('flags', DWORD), ('show', W.WORD),
                ('reserved2size', W.WORD), ('reserved2', C.c_void_p), ('stdin', HANDLE),
                ('stdout', HANDLE), ('stderr', HANDLE)]


class STARTUPINFOEX(C.Structure):
    _fields_ = [('startup', STARTUPINFO), ('attributes', C.c_void_p)]


class PROCESS_INFORMATION(C.Structure):
    _fields_ = [('process', HANDLE), ('thread', HANDLE), ('pid', DWORD), ('tid', DWORD)]


class Handle:
    def __init__(self, api, value):
        if not value or value == INVALID_HANDLE:
            raise C.WinError(C.get_last_error())
        self.api, self.value = api, value
        api.opened += 1

    def close(self):
        if self.value is not None:
            if not self.api.k.CloseHandle(self.value):
                self.api.close_failed += 1
                raise C.WinError(C.get_last_error())
            self.api.closed += 1
            self.value = None

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


class WinAPI:
    def __init__(self):
        if os.name != 'nt':
            raise OSError('Windows is required')
        self.k = C.WinDLL('kernel32', use_last_error=True)
        self.opened = self.closed = self.close_failed = 0
        self._bind()

    def _bind(self):
        p = C.c_void_p
        signatures = {
            'CloseHandle': ([HANDLE], W.BOOL), 'GetCurrentProcess': ([], HANDLE),
            'CreateJobObjectW': ([p, W.LPCWSTR], HANDLE),
            'SetInformationJobObject': ([HANDLE, C.c_int, p, DWORD], W.BOOL),
            'QueryInformationJobObject': ([HANDLE, C.c_int, p, DWORD, C.POINTER(DWORD)], W.BOOL),
            'AssignProcessToJobObject': ([HANDLE, HANDLE], W.BOOL),
            'IsProcessInJob': ([HANDLE, HANDLE, C.POINTER(W.BOOL)], W.BOOL),
            'OpenProcess': ([DWORD, W.BOOL, DWORD], HANDLE),
            'QueryFullProcessImageNameW': ([HANDLE, DWORD, W.LPWSTR, C.POINTER(DWORD)], W.BOOL),
            'WaitForSingleObject': ([HANDLE, DWORD], DWORD), 'GetExitCodeProcess': ([HANDLE, C.POINTER(DWORD)], W.BOOL),
            'GetProcessTimes': ([HANDLE, C.POINTER(W.FILETIME), C.POINTER(W.FILETIME),
                                 C.POINTER(W.FILETIME), C.POINTER(W.FILETIME)], W.BOOL),
            'TerminateProcess': ([HANDLE, W.UINT], W.BOOL), 'TerminateJobObject': ([HANDLE, W.UINT], W.BOOL),
            'CreateFileW': ([W.LPCWSTR, DWORD, DWORD, p, DWORD, DWORD, HANDLE], HANDLE),
            'GetFileInformationByHandle': ([HANDLE, C.POINTER(FILE_INFO)], W.BOOL),
            'GetFinalPathNameByHandleW': ([HANDLE, W.LPWSTR, DWORD, DWORD], DWORD),
            'ReadFile': ([HANDLE, p, DWORD, C.POINTER(DWORD), p], W.BOOL),
            'WriteFile': ([HANDLE, p, DWORD, C.POINTER(DWORD), p], W.BOOL),
            'FlushFileBuffers': ([HANDLE], W.BOOL),
            'DuplicateHandle': ([HANDLE, HANDLE, HANDLE, C.POINTER(HANDLE), DWORD, W.BOOL, DWORD], W.BOOL),
            'InitializeProcThreadAttributeList': ([p, DWORD, DWORD, C.POINTER(SIZE_T)], W.BOOL),
            'UpdateProcThreadAttribute': ([p, DWORD, SIZE_T, p, SIZE_T, p, p], W.BOOL),
            'DeleteProcThreadAttributeList': ([p], None),
            'CreateProcessW': ([W.LPCWSTR, W.LPWSTR, p, p, W.BOOL, DWORD, p, W.LPCWSTR,
                                C.POINTER(STARTUPINFO), C.POINTER(PROCESS_INFORMATION)], W.BOOL),
            'ResumeThread': ([HANDLE], DWORD),
        }
        for name, (args, result) in signatures.items():
            function = getattr(self.k, name)
            function.argtypes, function.restype = args, result

    @staticmethod
    def check(value):
        if not value:
            raise C.WinError(C.get_last_error())
        return value

    def create_job(self):
        job = Handle(self, self.k.CreateJobObjectW(None, None))
        try:
            limits = EXTENDED_LIMIT()
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
            self.check(self.k.SetInformationJobObject(job.value, 9, C.byref(limits), C.sizeof(limits)))
            return job
        except BaseException:
            job.close()
            raise

    def accounting(self, job):
        data = ACCOUNTING()
        self.check(self.k.QueryInformationJobObject(job.value, 1, C.byref(data), C.sizeof(data), None))
        return int(data.ActiveProcesses), int(data.TotalProcesses)

    def members(self, job, maximum=4096):
        count = 16
        while count <= maximum:
            class LIST(C.Structure):
                _fields_ = [('assigned', DWORD), ('count', DWORD), ('ids', ULONG_PTR * count)]
            data = LIST()
            ok = self.k.QueryInformationJobObject(job.value, 3, C.byref(data), C.sizeof(data), None)
            if ok and data.count == data.assigned and data.count <= count:
                ids = tuple(int(value) for value in data.ids[:data.count])
                if len(set(ids)) != len(ids) or any(not value or value > 0xFFFFFFFF for value in ids):
                    raise OSError('invalid job membership')
                return ids
            if not ok and C.get_last_error() != 234:
                raise C.WinError(C.get_last_error())
            count *= 2
        raise OSError('job membership exceeds bounded enumeration')

    def open_process(self, pid):
        rights = PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE
        return Handle(self, self.k.OpenProcess(rights, False, pid))

    def in_job(self, process, job):
        result = W.BOOL()
        self.check(self.k.IsProcessInJob(process.value, job.value, C.byref(result)))
        return bool(result.value)

    def in_any_job(self):
        result = W.BOOL()
        self.check(self.k.IsProcessInJob(self.k.GetCurrentProcess(), None, C.byref(result)))
        return bool(result.value)

    def image_path(self, process):
        buffer = C.create_unicode_buffer(32768)
        size = DWORD(len(buffer))
        self.check(self.k.QueryFullProcessImageNameW(process.value, 0, buffer, C.byref(size)))
        return buffer.value

    def wait(self, process, milliseconds):
        result = self.k.WaitForSingleObject(process.value, milliseconds)
        if result == WAIT_OBJECT_0:
            return True
        if result == WAIT_TIMEOUT:
            return False
        raise C.WinError(C.get_last_error())

    def exit_code(self, process):
        code = DWORD()
        self.check(self.k.GetExitCodeProcess(process.value, C.byref(code)))
        return int(code.value)

    def creation_time(self, process):
        created, exited, kernel, user = (W.FILETIME() for _ in range(4))
        self.check(self.k.GetProcessTimes(process.value, C.byref(created), C.byref(exited),
                                          C.byref(kernel), C.byref(user)))
        return (int(created.dwHighDateTime) << 32) | int(created.dwLowDateTime)

    def terminate(self, process, code):
        self.check(self.k.TerminateProcess(process.value, code))

    def terminate_job(self, job, code):
        self.check(self.k.TerminateJobObject(job.value, code))
