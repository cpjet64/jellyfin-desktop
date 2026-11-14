"""Finite, benign child modes for explicitly authorized Windows qualification."""
import subprocess
import sys
import time


def spawn(mode, output):
    return subprocess.Popen([sys.executable, __file__, mode, output], close_fds=True,
                            creationflags=subprocess.CREATE_NO_WINDOW)


def main(mode, output):
    if mode == "natural":
        with open(output, "xb") as stream:
            stream.write(b"fixture\n")
        return 0
    if mode == "rootfail":
        return 7
    if mode == "timeout":
        time.sleep(20)
        return 0
    if mode == "residual":
        with open(output, "xb") as stream:
            stream.write(b"fixture\n")
        spawn("timeout", output)
        return 0
    if mode == "transientchild":
        child = spawn("natural", output)
        child.wait(timeout=3)
        return 0
    if mode == "spawnlate":
        time.sleep(0.2)
        child = spawn("natural", output)
        child.wait(timeout=3)
        return 0
    raise ValueError("unknown fixture mode")


if __name__ == "__main__":
    if sys.platform != "win32" or len(sys.argv) != 3:
        raise SystemExit(125)
    raise SystemExit(main(sys.argv[1], sys.argv[2]))
