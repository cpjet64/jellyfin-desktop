"""Suspended, hidden build-root creation with a strict handle allowlist."""
import ctypes as C
import os
import subprocess

from . import winapi as w

CREATE_UNICODE_ENVIRONMENT = 0x400


class RootProcess:
    def __init__(self, api, process, thread, pid):
        self.api, self.process, self.thread, self.pid = api, process, thread, pid

    def wait(self, milliseconds):
        return self.api.wait(self.process, milliseconds)

    def exit_code(self):
        if not self.wait(0):
            raise OSError('root process has not exited')
        return self.api.exit_code(self.process)

    def close(self):
        failures = []
        for handle in (self.thread, self.process):
            try:
                handle.close()
            except OSError as error:
                failures.append(error)
        if failures:
            raise failures[0]

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def _duplicate(api, raw):
    result = w.HANDLE()
    current = api.k.GetCurrentProcess()
    api.check(api.k.DuplicateHandle(current, raw, current, C.byref(result), 0, True, w.DUPLICATE_SAME_ACCESS))
    return w.Handle(api, result.value)


def _environment(env):
    if env is None:
        return None
    if any(not isinstance(k, str) or not isinstance(v, str) or not k or '=' in k or '\0' in k + v
           for k, v in env.items()):
        raise ValueError('invalid environment')
    if len({key.casefold() for key in env}) != len(env):
        raise ValueError('duplicate environment key')
    return C.create_unicode_buffer('\0'.join(f'{key}={env[key]}' for key in sorted(env, key=str.casefold)) + '\0\0')


def _attributes(api, handles):
    size = w.SIZE_T()
    api.k.InitializeProcThreadAttributeList(None, 1, 0, C.byref(size))
    if not size.value:
        raise C.WinError(C.get_last_error())
    storage = C.create_string_buffer(size.value)
    api.check(api.k.InitializeProcThreadAttributeList(storage, 1, 0, C.byref(size)))
    values = (w.HANDLE * len(handles))(*(item.value for item in handles))
    try:
        api.check(api.k.UpdateProcThreadAttribute(storage, 0, w.PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                                  values, C.sizeof(values), None, None))
    except BaseException:
        api.k.DeleteProcThreadAttributeList(storage)
        raise
    return storage, values


def _create(api, job, executable, args, cwd, handles, env):
    storage, values = _attributes(api, handles)
    startup = w.STARTUPINFOEX()
    startup.startup.cb = C.sizeof(startup)
    startup.startup.flags = w.STARTF_USESTDHANDLES
    startup.startup.stdin, startup.startup.stdout, startup.startup.stderr = (h.value for h in handles)
    startup.attributes = C.cast(storage, C.c_void_p)
    info = w.PROCESS_INFORMATION()
    command = C.create_unicode_buffer(subprocess.list2cmdline([executable, *args]))
    environment = _environment(env)
    flags = w.CREATE_SUSPENDED | w.CREATE_NO_WINDOW | w.EXTENDED_STARTUPINFO_PRESENT
    if environment is not None:
        flags |= CREATE_UNICODE_ENVIRONMENT
    try:
        api.check(api.k.CreateProcessW(executable, command, None, None, True, flags, environment, cwd,
                                       C.byref(startup.startup), C.byref(info)))
    finally:
        api.k.DeleteProcThreadAttributeList(storage)
    process = w.Handle(api, info.process)
    thread = w.Handle(api, info.thread)
    root = RootProcess(api, process, thread, int(info.pid))
    try:
        api.check(api.k.AssignProcessToJobObject(job.value, process.value))
        if api.k.ResumeThread(thread.value) == 0xFFFFFFFF:
            raise C.WinError(C.get_last_error())
        return root
    except BaseException:
        try:
            api.terminate(process, 125)
            if not api.wait(process, 10000):
                raise OSError('failed root did not settle')
        finally:
            root.close()
        raise


def launch(api, job, executable, args, cwd, stdout_handle, stderr_handle, env=None):
    if os.name != 'nt':
        raise OSError('Windows is required')
    if not os.path.isabs(executable) or not os.path.isabs(cwd):
        raise ValueError('absolute executable and working directory required')
    attrs = w.SECURITY_ATTRIBUTES(C.sizeof(w.SECURITY_ATTRIBUTES), None, True)
    handles = []
    root = None
    try:
        raw = api.k.CreateFileW('NUL', w.GENERIC_READ, w.FILE_SHARE_READ | 2, C.byref(attrs), w.OPEN_EXISTING, 0, None)
        handles.append(w.Handle(api, raw))
        handles.append(_duplicate(api, stdout_handle))
        handles.append(_duplicate(api, stderr_handle))
        root = _create(api, job, executable, list(args), cwd, handles, env)
    finally:
        errors = []
        while handles:
            try:
                handles.pop().close()
            except OSError as error:
                errors.append(error)
        if errors:
            if root is not None:
                try:
                    api.terminate_job(job, 125)
                    if not root.wait(10000):
                        raise OSError('root did not settle after handle close failure')
                finally:
                    root.close()
            raise errors[0]
    return root
