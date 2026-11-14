"""Suspended, hidden build-root creation with a strict handle allowlist."""
import ctypes as C
import os
import subprocess
import time

from . import winapi as w

CREATE_UNICODE_ENVIRONMENT = 0x400


class LaunchFailure(OSError):
    """Failed launch with ownership of any unresolved exact native handles."""
    def __init__(self, message, root=None, retained_handles=(), job_unsettled=False, cleanup_deadline=None):
        super().__init__(message)
        self.root = root
        self.retained_handles = tuple(retained_handles)
        self.job_unsettled = job_unsettled
        self.cleanup_deadline = cleanup_deadline


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


def _create(api, job, executable, args, cwd, handles, env, root_verifier):
    command = C.create_unicode_buffer(subprocess.list2cmdline([executable, *args]))
    environment = _environment(env)
    storage, values = _attributes(api, handles)
    try:
        startup = w.STARTUPINFOEX()
        startup.startup.cb = C.sizeof(startup)
        startup.startup.flags = w.STARTF_USESTDHANDLES
        startup.startup.stdin, startup.startup.stdout, startup.startup.stderr = (h.value for h in handles)
        startup.attributes = C.cast(storage, C.c_void_p)
        info = w.PROCESS_INFORMATION()
        flags = w.CREATE_SUSPENDED | w.CREATE_NO_WINDOW | w.EXTENDED_STARTUPINFO_PRESENT
        if environment is not None:
            flags |= CREATE_UNICODE_ENVIRONMENT
        api.check(api.k.CreateProcessW(executable, command, None, None, True, flags, environment, cwd,
                                       C.byref(startup.startup), C.byref(info)))
    finally:
        api.k.DeleteProcThreadAttributeList(storage)
    process = w.Handle(api, info.process)
    thread = w.Handle(api, info.thread)
    root = RootProcess(api, process, thread, int(info.pid))
    assigned = False
    try:
        api.check(api.k.AssignProcessToJobObject(job.value, process.value))
        assigned = True
        root_verifier(process, job)
        previous = api.k.ResumeThread(thread.value)
        if previous != 1:
            raise OSError(f'unexpected root suspend count: {previous}')
        return root
    except BaseException as original:
        deadline = time.monotonic() + 10
        if assigned:
            retained, unsettled = _emergency_settle(api, job, root, deadline)
            raise LaunchFailure('assigned root launch failed', retained, (), unsettled, deadline) from original
        settled = False
        try:
            api.terminate(process, 125)
        except OSError:
            pass
        try:
            remaining = deadline - time.monotonic()
            if remaining > 0:
                settled = api.wait(process, max(0, int(remaining * 1000)))
        except OSError:
            pass
        if settled:
            try:
                root.close()
            except OSError as error:
                raise LaunchFailure('failed root handle close', root=root, job_unsettled=True,
                                    cleanup_deadline=deadline) from error
            raise LaunchFailure('root launch failed and settled', cleanup_deadline=deadline) from original
        raise LaunchFailure('root launch failed; root settlement unknown', root=root, job_unsettled=True,
                            cleanup_deadline=deadline) from original


def _close_handles(handles, deadline):
    failed = []
    for handle in reversed(handles):
        try:
            handle.close()
        except OSError:
            if deadline is None:
                deadline = time.monotonic() + 10
            failed.append(handle)
    return list(reversed(failed)), deadline


def _emergency_settle(api, job, root, deadline):
    if time.monotonic() >= deadline:
        return root, True
    try:
        api.terminate_job(job, 125)
    except OSError:
        pass
    try:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return root, True
        remaining = max(0, int(remaining * 1000))
        if not root.wait(remaining):
            return root, True
        root.close()
    except OSError:
        return root, True
    while time.monotonic() < deadline:
        try:
            active, _ = api.accounting(job)
            if active == 0:
                return None, False
        except OSError:
            return None, True
        time.sleep(min(0.02, max(0, deadline - time.monotonic())))
    return None, True


def launch(api, job, executable, args, cwd, stdout_handle, stderr_handle, root_verifier, env=None):
    if os.name != 'nt':
        raise OSError('Windows is required')
    if not os.path.isabs(executable) or not os.path.isabs(cwd):
        raise ValueError('absolute executable and working directory required')
    if not callable(root_verifier):
        raise ValueError('root verifier is required')
    attrs = w.SECURITY_ATTRIBUTES(C.sizeof(w.SECURITY_ATTRIBUTES), None, True)
    handles = []
    root = None
    failure = None
    deadline = None
    try:
        raw = api.k.CreateFileW('NUL', w.GENERIC_READ, w.FILE_SHARE_READ | 2, C.byref(attrs), w.OPEN_EXISTING, 0, None)
        handles.append(w.Handle(api, raw))
        handles.append(_duplicate(api, stdout_handle))
        handles.append(_duplicate(api, stderr_handle))
        root = _create(api, job, executable, list(args), cwd, handles, env, root_verifier)
    except BaseException as error:
        failure = error
        deadline = error.cleanup_deadline if isinstance(error, LaunchFailure) else time.monotonic() + 10
    finally:
        handles, deadline = _close_handles(handles, deadline)
    if handles:
        unsettled = False
        if root is not None:
            root, unsettled = _emergency_settle(api, job, root, deadline)
        if isinstance(failure, LaunchFailure):
            unsettled = unsettled or failure.job_unsettled
            if failure.root is not None:
                root = failure.root
            for handle in failure.retained_handles:
                if not any(handle is existing for existing in handles):
                    handles.append(handle)
        raise LaunchFailure('inherited handle close failed', root, handles, unsettled, deadline) from failure
    if failure is not None:
        if isinstance(failure, LaunchFailure):
            raise failure
        raise LaunchFailure('launch preparation failed', cleanup_deadline=deadline) from failure
    return root
