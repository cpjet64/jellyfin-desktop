"""Build a pinned fixture-only policy in an explicit project-local directory."""
import hashlib
import json
import os
from pathlib import Path
import sys
from .identity import FilePin
from .policy import CommandIdentity, Policy, environment_digest
from .supervisor import SOURCE_FILES
from .winapi import WinAPI


def _hash(path):
    digest = hashlib.sha256()
    size = 0
    with open(path, "rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
            size += len(block)
    return digest.hexdigest(), size


def _source_hash(directory):
    records = [[name, _hash(directory / name)[0]] for name in SOURCE_FILES]
    data = json.dumps(records, separators=(",", ":"), ensure_ascii=True).encode("ascii")
    return hashlib.sha256(data).hexdigest()


def make_policy(workdir, mode):
    if sys.platform != "win32" or mode not in ("natural", "residual", "transientchild", "spawnlate", "rootfail", "timeout"):
        raise ValueError("unsupported fixture or platform")
    directory = Path(__file__).resolve().parent
    project = directory.parents[3]
    target = Path(workdir)
    if not target.is_absolute():
        raise ValueError("absolute fixture work directory required")
    target = target.resolve(strict=True)
    if not target.is_dir() or not target.is_relative_to(project / "deps"):
        raise ValueError("fixture directory must be under project deps")
    executable = Path(sys.executable).resolve(strict=True)
    child = (directory / "fixture_child.py").resolve(strict=True)
    exe_hash, exe_size = _hash(executable)
    response_hash, _ = _hash(child)
    api = WinAPI()
    with FilePin(api, str(executable), exe_hash, exe_size) as pin:
        file_id = ":".join(str(part) for part in pin.file_id)
    names = ("CL", "INCLUDE", "LIB", "LIBPATH", "LINK", "PATH", "SystemRoot", "TEMP", "TMP", "_CL_", "_LINK_")
    names = tuple(sorted(names, key=str.casefold))
    output = str(target / "result.bin")
    command = CommandIdentity(str(executable), exe_hash, exe_size,
                              ("-B", str(child), mode, output), str(target),
                              str(target / "stdout.log"), str(target / "stderr.log"),
                              str(child), response_hash, (output,),
                              environment_digest(names, os.environ), names)
    return Policy("fixture-" + mode, "fixture", "fixture", _source_hash(directory),
                  str(executable), exe_hash, exe_size, "fixture-python", file_id,
                  command, 4000, 100)
