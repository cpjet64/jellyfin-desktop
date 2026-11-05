"""Run the pinned selector on Windows; poll the child against a 180-second deadline.

Popen interruption before it returns a handle cannot be settled by this launcher.
The held direct child is the only process covered; no descendant guarantee is made.
The deadline is checked only while polling; preflight and a quick exit are not bounded.
Synthetic tests do not validate Windows pipe APIs or establish runtime proof.
"""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import types

selector = None

REPO = Path(__file__).resolve().parents[2]
SOURCE = REPO / "deps" / "w463-sdist-proof" / "sources"
OUTPUT = REPO / "deps" / "w534-selector-proof"
PYTHON = Path("C:/Python314/python.exe")
PINS = {
    PYTHON: "7ca24f26d6e3f463419ee4f537ddd3acd312c38fe45e678cce08572f26a8bd1a",
    REPO / "dev/ci/macos_sdist_select.py": "9b8364f28d23f67d8ae35793cf50dca02b8b7913b6d6ee0ef818bccd9f1304d9",
    REPO / "dev/ci/macos_sdist_static.py": "5848f746a9c911841addd21d36a81a5e05ec9143a8de2116e4165b96ae48fcaf",
    REPO / "dev/ci/macos_sdist_manifest.json": "b613594af22a0c36ca81fb8c2e7f90d4a09ddafd41cb2d8f033e7fd80e8e201c",
}
DEADLINE = 180
STDOUT_LIMIT = 16 * 1024 * 1024
STDERR_LIMIT = 1024 * 1024
HEADROOM = 2 * 1024 * 1024 * 1024
BOOTSTRAP = "import hashlib,sys,types; p=sys.argv[1:3]; h=sys.argv[3:5]; b=[open(x,'rb').read() for x in p]; assert all(hashlib.sha256(x).hexdigest()==y for x,y in zip(b,h)); m=types.ModuleType('macos_sdist_static'); m.__file__=p[0]; sys.modules[m.__name__]=m; exec(compile(b[0],p[0],'exec'),m.__dict__); sys.argv=sys.argv[5:]; exec(compile(b[1],p[1],'exec'),{'__name__':'__main__','__file__':p[1]})"


def load_verified(path, name):
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != PINS[path]:
        raise ValueError("input hash mismatch")
    module = types.ModuleType(name)
    module.__file__ = str(path)
    sys.modules[name] = module
    try:
        exec(compile(data, str(path), "exec"), module.__dict__)
    except BaseException:
        del sys.modules[name]
        raise
    return module


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def identity(handle, pid):
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetProcessTimes.argtypes = [ctypes.c_void_p] + [ctypes.c_void_p] * 4
    kernel.QueryFullProcessImageNameW.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_ulong)]
    created, exited, kernel_time, user_time = (ctypes.c_ulonglong() for _ in range(4))
    if not kernel.GetProcessTimes(handle, *map(ctypes.byref, (created, exited, kernel_time, user_time))):
        raise ctypes.WinError(ctypes.get_last_error())
    size = ctypes.c_ulong(32768)
    name = ctypes.create_unicode_buffer(size.value)
    if not kernel.QueryFullProcessImageNameW(handle, 0, name, ctypes.byref(size)):
        raise ctypes.WinError(ctypes.get_last_error())
    return {"pid": pid, "creation_filetime": created.value, "image": name.value}


def preflight():
    global selector
    if os.name != "nt":
        raise RuntimeError("Windows required")
    if OUTPUT.exists():
        raise ValueError("output already exists")
    if shutil.disk_usage(REPO / "deps").free < HEADROOM + STDOUT_LIMIT + STDERR_LIMIT:
        raise ValueError("insufficient space")
    for path, expected in PINS.items():
        if digest(path) != expected:
            raise ValueError("input hash mismatch")
    load_verified(REPO / "dev/ci/macos_sdist_static.py", "macos_sdist_static")
    selector = load_verified(REPO / "dev/ci/macos_sdist_select.py", "macos_sdist_select")
    selector.validate_directory(REPO / "deps")
    selector.validate_directory(SOURCE)
    for name, spec in selector.ARCHIVES.items():
        path = SOURCE / name
        if path.stat().st_size != spec["size"] or digest(path) != spec["sha256"]:
            raise ValueError("archive hash mismatch")


def available(stream):
    import msvcrt
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.PeekNamedPipe.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_ulong,
                                     ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong), ctypes.c_void_p]
    count = ctypes.c_ulong()
    if not kernel.PeekNamedPipe(msvcrt.get_osfhandle(stream.fileno()), None, 0, None,
                                ctypes.byref(count), None):
        error = ctypes.get_last_error()
        if error == 109:  # broken pipe
            return 0
        raise ctypes.WinError(error)
    return count.value


def collect(process, state):
    progress = False
    for stream, limit, key in ((process.stdout, STDOUT_LIMIT, "stdout"),
                               (process.stderr, STDERR_LIMIT, "stderr")):
        count = available(stream)
        if count:
            chunk = stream.read1(min(count, 65536))
            if not chunk:
                continue
            progress = True
            remaining = limit - len(state[key])
            state[key] += chunk[:remaining]
            if len(chunk) > remaining:
                state["overflow"] = True
    return progress


def collect_remaining(process, state):
    while collect(process, state) and not state["overflow"]:
        pass


def run_child(command, clock=time.monotonic):
    start = clock()
    state = {"overflow": False, "stdout": b"", "stderr": b""}
    failure = None
    process = None
    child = {"identity_unavailable": True}
    try:
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, close_fds=True,
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        child = {"pid": process.pid, "identity_unavailable": True}
        child = identity(process._handle, process.pid)
        if Path(child["image"]).resolve() != PYTHON.resolve():
            raise ValueError("child image mismatch")
        while process.poll() is None:
            collect(process, state)
            if clock() - start >= DEADLINE or state["overflow"]:
                raise RuntimeError("timeout or output failure")
            time.sleep(0.05)
        code = process.wait(timeout=5)
        collect_remaining(process, state)
        if state["overflow"]:
            raise RuntimeError("output overflow")
    except BaseException as error:
        failure = repr(error)
        try:
            if process is None:
                code = None
            else:
                child.setdefault("pid", process.pid)
                if process.poll() is None:
                    process.kill()
                code = process.wait(timeout=5)
        except BaseException as settlement_error:
            failure += "; child settlement uncertain: " + repr(settlement_error)
            code = None
        if process is not None:
            try:
                collect_remaining(process, state)
            except BaseException as output_error:
                failure += "; output collection failed: " + repr(output_error)
    if process is not None:
        process.stdout.close()
        process.stderr.close()
    return child, code, clock() - start, state, failure


def validate_output(raw):
    result = json.loads(raw)
    records = result["members"]
    expected = {(name, spec["root"] + "/" + member) for name, spec in selector.ARCHIVES.items()
                for member in spec["members"]}
    if len(records) != 9 or {(r["archive"], r["member"]) for r in records} != expected:
        raise ValueError("unexpected selected members")
    if not 0 <= result["expanded"] <= selector.bounded.EXPANSION_LIMIT:
        raise ValueError("expansion limit")
    for record in records:
        spec = selector.ARCHIVES[record["archive"]]
        raw_text = record["text"].encode("utf-8")
        if (record["archive_sha256"] != spec["sha256"] or record["archive_bytes"] != spec["size"] or
                record["bytes"] != len(raw_text) or len(raw_text) > selector.bounded.METADATA_LIMIT or
                record["sha256"] != hashlib.sha256(raw_text).hexdigest()):
            raise ValueError("invalid member evidence")
    return result


def main():
    preflight()
    parent = identity(ctypes.windll.kernel32.GetCurrentProcess(), os.getpid())
    if Path(parent["image"]).resolve() != PYTHON.resolve():
        raise ValueError("parent image mismatch")
    static = REPO / "dev/ci/macos_sdist_static.py"
    script = REPO / "dev/ci/macos_sdist_select.py"
    command = [str(PYTHON), "-I", "-S", "-B", "-c", BOOTSTRAP, str(static), str(script),
               PINS[static], PINS[script], str(script),
               "--source-dir", str(SOURCE)]
    OUTPUT.mkdir()
    child, code, elapsed, state, failure = run_child(command)
    if not failure and not code:
        try:
            validate_output(state["stdout"])
        except Exception as error:
            failure = repr(error)
    (OUTPUT / "selected.json").write_bytes(state.get("stdout", b""))
    (OUTPUT / "stderr.log").write_bytes(state.get("stderr", b""))
    receipt = {"parent": parent, "child": child, "exit": code, "elapsed_seconds": elapsed,
               "stdout_sha256": hashlib.sha256(state.get("stdout", b"")).hexdigest(),
               "stdout_bytes": len(state.get("stdout", b"")), "stderr_bytes": len(state.get("stderr", b"")),
               "failure": failure,
               "scope": "held direct child; no general descendant-tree proof"}
    (OUTPUT / "receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
    if failure or code:
        raise RuntimeError("selector failed; evidence retained")


if __name__ == "__main__":
    main()
