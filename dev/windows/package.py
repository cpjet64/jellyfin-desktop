"""Invalidate configured Windows package outputs, then build both packages."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


def package(build, config, parallel):
    build = build.resolve(strict=True)
    names = (build / "windows-package-outputs.txt").read_text(encoding="utf-8").splitlines()
    if (len(names) != 2 or not re.fullmatch(r"JellyfinDesktop-[A-Za-z0-9_.+-]+\.exe", names[0])
            or names[1] != names[0][:-4] + ".zip"):
        raise ValueError("Invalid configured Windows package output names")
    for name in names:
        (build / name).unlink(missing_ok=True)
    command = ["cmake", "--build", str(build), "--target", "windows_all"]
    if config:
        command += ["--config", config]
    if parallel:
        command += ["--parallel", str(parallel)]
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    result = subprocess.run(command, check=False, creationflags=flags).returncode
    if result == 0 and not all((build / name).is_file() for name in names):
        raise ValueError("Packaging succeeded without both configured outputs")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path, help="configured CMake build directory")
    parser.add_argument("--config", help="build configuration for multi-config generators")
    parser.add_argument("--parallel", type=int, default=4)
    args = parser.parse_args()
    if args.parallel < 1:
        parser.error("--parallel must be positive")
    return package(args.build, args.config, args.parallel)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError) as error:
        print(f"Windows package driver failed: {error}", file=sys.stderr)
        sys.exit(1)
