"""Held-session proposal for trusted fixed producer commands on Linux.

Derived from portable_proof/executor.py; not qualified for this build. A process
that deliberately escapes its session is outside its containment guarantee.
"""

import json
import os
import select
import signal
import subprocess
import time


def child(write_fd, ack_fd, command, env, log, executor):
    try:
        os.setsid()
        os.write(write_fd, b"R")
        if os.read(ack_fd, 1) != b"G":
            raise RuntimeError("session release missing")
        with log.open("xb") as output:
            process = subprocess.Popen(command.argv, cwd=command.cwd, env=env,
                                       stdin=subprocess.DEVNULL, stdout=output,
                                       stderr=subprocess.STDOUT, close_fds=True)
            code = process.wait(timeout=command.timeout)
        result = {"exit": code, "survivors": executor.group_survivors()}
    except BaseException as error:
        result = {"error": type(error).__name__}
    os.write(write_fd, json.dumps(result).encode("ascii"))
    while True:
        signal.pause()  # Keep the session leader alive until targeted cleanup.


def run(command, env, log, deadline, executor):
    """Execute one phase, holding its group identity through cleanup."""
    remaining = deadline - time.monotonic()
    if remaining <= 0:
        raise TimeoutError("overall deadline exhausted")
    read_fd, write_fd = os.pipe()
    ack_read, ack_write = os.pipe()
    pid = None
    ready_group = False
    try:
        pid = os.fork()
        if pid == 0:
            os.close(read_fd)
            os.close(ack_write)
            try:
                child(write_fd, ack_read, command, env, log, executor)
            finally:
                os._exit(1)
        os.close(write_fd)
        write_fd = None
        os.close(ack_read)
        ack_read = None
        ready, _, _ = select.select([read_fd], [], [], min(30, remaining))
        if not ready or os.read(read_fd, 1) != b"R":
            raise TimeoutError("session setup failed")
        ready_group = True
        os.write(ack_write, b"G")
        remaining = max(0, deadline - time.monotonic())
        ready, _, _ = select.select([read_fd], [], [], min(command.timeout + 10, remaining))
        if not ready:
            raise TimeoutError("phase deadline exceeded")
        result = json.loads(os.read(read_fd, 8192).decode("ascii"))
        if result.get("exit") != 0 or result.get("survivors") != []:
            raise RuntimeError("phase failed or left unknown group members")
    finally:
        if pid:
            executor.stop_supervisor(pid, ready_group)
        for fd in (read_fd, write_fd, ack_read, ack_write):
            if fd is not None:
                os.close(fd)
