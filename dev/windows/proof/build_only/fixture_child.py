"""Finite, benign child modes for explicitly authorized Windows qualification."""
import subprocess
import sys
import time


def spawn(mode):
    return subprocess.Popen([sys.executable, __file__, mode], close_fds=True,
                            creationflags=subprocess.CREATE_NO_WINDOW)


def main(mode):
    if mode == "natural":
        return 0
    if mode == "rootfail":
        return 7
    if mode == "timeout":
        time.sleep(5)
        return 0
    if mode == "residual":
        spawn("timeout")
        return 0
    if mode == "transientchild":
        child = spawn("natural")
        child.wait(timeout=3)
        return 0
    if mode == "spawnlate":
        time.sleep(0.2)
        child = spawn("natural")
        child.wait(timeout=3)
        return 0
    raise ValueError("unknown fixture mode")


if __name__ == "__main__":
    if sys.platform != "win32" or len(sys.argv) != 2:
        raise SystemExit(125)
    raise SystemExit(main(sys.argv[1]))
