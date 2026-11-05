"""Run the exact portable CI suites in a fresh native runner-owned workspace."""
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import platform
import pwd
import re
import resource
import select
import selectors
import signal
import stat
import subprocess
import sys
import time
import zipfile
import assembly

SOURCE = "8358bd61286fb2d706f245a278aa577ef27363e1"
BUNDLE_SHA = "ee9daf887e9a2841f22b3dda9dedeaa203d278eabdf570aea3177fdbd178f07c"
MANIFEST_SHA = "190d002c29585a051adec1fed896fdaffd7adc65ec38affec7840271f159d11b"
RUNNER_SHA = "31cb7fe63bdcc4186bdef12d1440429dfae8d9009a5b3c338a36517f01fa45f1"
DEADLINE = time.monotonic() + 1200
STATE = {"source": SOURCE, "suites": []}


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def account_workspace(kind, task):
    if kind != "linux" or not re.fullmatch("[0-9a-f]{32}", task):
        raise ValueError("invalid platform or task")
    username, expected_home = "gitlab-runner", "/home/gitlab-runner"
    if platform.system() != "Linux" or platform.machine().lower() != "x86_64":
        raise RuntimeError("wrong native platform")
    account = pwd.getpwnam(username)
    if os.geteuid() == 0:
        os.initgroups(username, account.pw_gid)
        os.setgid(account.pw_gid)
        os.setuid(account.pw_uid)
    if os.getuid() != account.pw_uid or os.geteuid() != account.pw_uid or account.pw_dir != expected_home:
        raise RuntimeError("runner identity mismatch")
    home = Path(expected_home)
    if home.is_symlink() or home.resolve(strict=True) != home or home.stat().st_uid != account.pw_uid:
        raise RuntimeError("runner home is not canonical and owned")
    if __import__("shutil").disk_usage(home).free < 2 * 1024**3:
        raise RuntimeError("insufficient free workspace")
    workspace = home / ("portable-proof-" + task)
    assembly.validate_workspace(workspace, task)
    return workspace, account


def unpack(workspace, bundle, manifest_bytes, runner_bytes):
    if hashlib.sha256(bundle).hexdigest() != BUNDLE_SHA or hashlib.sha256(manifest_bytes).hexdigest() != MANIFEST_SHA:
        raise RuntimeError("source bundle or manifest identity mismatch")
    if hashlib.sha256(runner_bytes).hexdigest() != RUNNER_SHA:
        raise RuntimeError("suite runner identity mismatch")
    manifest = json.loads(manifest_bytes)
    if manifest["commit"] != SOURCE or manifest["bundle_sha256"] != BUNDLE_SHA:
        raise RuntimeError("source manifest commit mismatch")
    checkout = workspace / "checkout"
    checkout.mkdir(mode=0o700)
    with zipfile.ZipFile(io.BytesIO(bundle)) as archive:
        entries = archive.infolist()
        names = [item.filename for item in entries]
        if len(names) != len(set(names)) or set(names) != set(manifest["files"]):
            raise RuntimeError("source bundle member mismatch")
        if sum(item.file_size for item in entries) > 2 * 1024**2:
            raise RuntimeError("source bundle expanded size exceeded")
        for item in entries:
            path = PurePosixPath(item.filename)
            meta = manifest["files"][item.filename]
            if path.is_absolute() or ".." in path.parts or item.file_size != meta["bytes"] or item.file_size > 1024**2:
                raise RuntimeError("invalid source member")
            data = archive.read(item)
            if hashlib.sha256(data).hexdigest() != meta["sha256"]:
                raise RuntimeError("source member hash mismatch")
            output = checkout.joinpath(*path.parts)
            output.parent.mkdir(parents=True, exist_ok=True)
            with output.open("xb") as stream:
                stream.write(data)
    (workspace / "suite_runner.py").write_bytes(runner_bytes)
    return checkout


def limit_child():
    resource.setrlimit(resource.RLIMIT_CPU, (90, 90))
    resource.setrlimit(resource.RLIMIT_FSIZE, (2 * 1024**2, 2 * 1024**2))


def group_survivors():
    ps = subprocess.Popen(["/bin/ps", "-axo", "pid=,pgid="], stdin=subprocess.DEVNULL,
                          stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    selector = selectors.DefaultSelector()
    selector.register(ps.stdout, selectors.EVENT_READ)
    output = bytearray()
    end = time.monotonic() + 5
    try:
        while selector.get_map():
            remaining = end - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("process-group inspection timed out")
            for key, _ in selector.select(remaining):
                data = os.read(key.fd, 4096)
                if not data:
                    selector.unregister(key.fileobj)
                elif len(output) + len(data) > 1024 * 1024:
                    raise RuntimeError("process-group inspection output exceeded limit")
                else:
                    output.extend(data)
        if ps.wait(timeout=max(0.01, end - time.monotonic())):
            raise RuntimeError("process-group inspection failed")
    finally:
        selector.close()
        ps.stdout.close()
    group = os.getpgrp()
    return [int(parts[0]) for line in output.splitlines() if len(parts := line.split()) == 2 and
            int(parts[1]) == group and int(parts[0]) not in (os.getpid(), ps.pid)]


def run_supervisor(write_fd, ack_fd, python, workspace, checkout, suite, env, kind, path):
    try:
        os.setsid()
        os.write(write_fd, b"R")
        if os.read(ack_fd, 1) != b"G":
            raise RuntimeError("supervisor release was not granted")
        with path.open("xb") as log:
            child = subprocess.Popen([str(python), "-I", "-S", "-B", str(workspace / "suite_runner.py"), str(checkout), suite, kind],
                                     cwd=checkout, env=env, stdout=log, stderr=subprocess.STDOUT, preexec_fn=limit_child)
            code = child.wait(timeout=90)
        result = {"exit": code, "survivors": group_survivors()}
    except BaseException as error:
        result = {"error": type(error).__name__}
    try:
        os.write(write_fd, json.dumps(result).encode("ascii"))
        while True:
            signal.pause()  # Controller kills this still-unreaped session leader.
    finally:
        os._exit(1)


def stop_supervisor(pid, group_ready):
    signal_error = None
    try:
        if group_ready:
            os.killpg(pid, signal.SIGKILL)
        else:
            os.kill(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    except BaseException as error:
        signal_error = error
    end = time.monotonic() + 5
    while True:
        reaped, _ = os.waitpid(pid, os.WNOHANG)
        if reaped == pid:
            break
        if time.monotonic() >= end:
            raise TimeoutError("supervisor reap unconfirmed; ownership retained; signal error: " + repr(signal_error))
        time.sleep(min(0.05, max(0, end - time.monotonic())))
    if signal_error is not None:
        raise RuntimeError("supervisor signal failed: " + repr(signal_error))
    if not group_ready:
        return
    end = time.monotonic() + 5
    while True:
        try:
            os.killpg(pid, 0)  # Observation only after reap; never signal it again.
        except ProcessLookupError:
            return
        if time.monotonic() >= end:
            raise RuntimeError("test process group did not settle")
        time.sleep(0.05)


def check_supervisor_result(result, suite):
    if type(result.get("exit")) is not int or result["exit"] != 0 or result.get("survivors") != []:
        raise RuntimeError("portable suite failed or left descendants: " + suite)


def suite_log_result(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    results = [line[len("W411_RESULT="):] for line in lines if line.startswith("W411_RESULT=")]
    if len(results) != 1:
        raise RuntimeError("missing or duplicate suite result")
    result = json.loads(results[0])
    result.update({"log_name": path.name, "log_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                   "log_bytes": path.stat().st_size})
    return result


def run_suite(python, workspace, checkout, suite, env, index, kind):
    remaining = DEADLINE - time.monotonic()
    if remaining <= 0:
        raise TimeoutError("overall test deadline exhausted")
    path = workspace / ("suite-%02d.log" % index)
    fds = []
    oldmask = None
    pid = None
    group_ready = False
    failure = None
    cleanup_errors = []
    try:
        read_fd, write_fd = os.pipe()
        fds.extend((read_fd, write_fd))
        ack_read, ack_write = os.pipe()
        fds.extend((ack_read, ack_write))
        oldmask = signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGALRM})
        pid = os.fork()
        if pid == 0:
            try:
                os.close(read_fd)
                os.close(ack_write)
                signal.pthread_sigmask(signal.SIG_SETMASK, oldmask)
                run_supervisor(write_fd, ack_read, python, workspace, checkout, suite, env, kind, path)
            finally:
                os._exit(1)
        os.close(write_fd)
        fds.remove(write_fd)
        os.close(ack_read)
        fds.remove(ack_read)
        signal.pthread_sigmask(signal.SIG_SETMASK, oldmask)
        ready, _, _ = select.select([read_fd], [], [], min(100, remaining))
        if not ready:
            raise TimeoutError("supervisor readiness deadline exceeded")
        if os.read(read_fd, 1) != b"R":
            raise RuntimeError("supervisor did not establish its session")
        group_ready = True
        os.write(ack_write, b"G")
        ready, _, _ = select.select([read_fd], [], [], min(100, max(0, DEADLINE - time.monotonic())))
        if not ready:
            raise TimeoutError("portable suite deadline exceeded")
        result = json.loads(os.read(read_fd, 8192).decode("ascii"))
    except BaseException as error:
        failure = error
    finally:
        if pid is not None and pid > 0:
            try:
                signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGALRM})
            except BaseException as error:
                cleanup_errors.append(error)
            try:
                stop_supervisor(pid, group_ready)
            except BaseException as error:
                cleanup_errors.append(error)
        for fd in fds:
            try:
                os.close(fd)
            except BaseException as error:
                cleanup_errors.append(error)
        if oldmask is not None:
            try:
                signal.pthread_sigmask(signal.SIG_SETMASK, oldmask)
            except BaseException as error:
                cleanup_errors.append(error)
    if cleanup_errors:
        detail = '; '.join(repr(error) for error in cleanup_errors)
        STATE['cleanup_unconfirmed'] = detail
        if failure is None:
            raise RuntimeError('supervisor cleanup unconfirmed: ' + detail)
    if failure is not None:
        raise failure
    check_supervisor_result(result, suite)
    return suite_log_result(path)


def main():
    if len(sys.argv) != 3 or not re.fullmatch('[0-9a-f]{64}', sys.argv[2]):
        raise ValueError("invalid payload argument count")
    kind, task = "linux", sys.argv[1]
    workspace, account = account_workspace(kind, task)
    bundle_path = workspace / 'source.bundle'
    bundle = assembly.read_chunk(bundle_path, assembly.TOTAL, assembly.SHA256)
    manifest = (Path(__file__).parent / 'manifest-8358bd6.json').read_bytes()
    runner = (Path(__file__).parent / 'suite_runner.py').read_bytes()
    suite_names = json.loads(manifest)['suites']
    if len(suite_names) != 26 or len(set(suite_names)) != 26:
        raise ValueError('invalid suite inventory')
    STATE.update({"platform": kind, "task": task, "workspace": str(workspace), "uid": account.pw_uid})
    checkout = unpack(workspace, bundle, manifest, runner)
    for name in ("tmp", "cache", "home"):
        (workspace / name).mkdir(mode=0o700)
    interpreter = Path("/usr/bin/python3").resolve(strict=True)
    info = interpreter.stat()
    if not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022 or not os.access(interpreter, os.X_OK):
        raise RuntimeError("unsafe Python interpreter")
    for parent in interpreter.parents:
        parent_info = parent.stat()
        if parent_info.st_uid != 0 or parent_info.st_mode & 0o022:
            raise RuntimeError("unsafe Python interpreter ancestor")
    python_hash = file_sha256(interpreter)
    if python_hash != sys.argv[2]:
        raise RuntimeError("Python interpreter differs from verified preflight")
    env = {"HOME": str(workspace / "home"), "TMPDIR": str(workspace / "tmp"), "XDG_CACHE_HOME": str(workspace / "cache"),
           "PATH": "/usr/bin:/bin", "LANG": "C", "PYTHONDONTWRITEBYTECODE": "1"}
    for index, suite in enumerate(suite_names):
        STATE["current_suite"] = suite
        STATE["suites"].append(run_suite(interpreter, workspace, checkout, suite, env, index, kind))
    if Path("/usr/bin/python3").resolve(strict=True) != interpreter or file_sha256(interpreter) != python_hash:
        raise RuntimeError("native Python executable changed during test run")
    report = {"platform": kind, "task": task, "source": SOURCE, "bundle_sha256": BUNDLE_SHA,
              "workspace": str(workspace), "uid": account.pw_uid, "python_sha256": python_hash,
              "suites": STATE["suites"]}
    with (workspace / "results.json").open("x", encoding="utf-8") as out:
        json.dump(report, out, sort_keys=True)
        out.flush()
        os.fsync(out.fileno())
    print(json.dumps(report, sort_keys=True), flush=True)


if __name__ == "__main__":
    def deadline(_signum, _frame):
        raise TimeoutError("overall test deadline exhausted")
    signal.signal(signal.SIGALRM, deadline)
    signal.setitimer(signal.ITIMER_REAL, 1200)
    try:
        main()
    except BaseException as error:
        print(json.dumps({"error": type(error).__name__, "detail": str(error)[:300], "partial": STATE}), flush=True)
        raise SystemExit(1)
