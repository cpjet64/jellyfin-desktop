"""Exact command admission for the separate build-only proof protocol."""
from dataclasses import dataclass
import hashlib
import json
import ntpath
import os
import re


@dataclass(frozen=True)
class CommandIdentity:
    executable: str
    executable_sha256: str
    executable_size: int
    args: tuple[str, ...]
    cwd: str
    stdout: str
    stderr: str
    response_path: str
    response_sha256: str
    outputs: tuple[str, ...]
    env_digest: str
    env_names: tuple[str, ...]


@dataclass(frozen=True)
class Policy:
    identity: str
    kind: str
    command_form: str
    source_sha256: str
    telemetry_path: str
    telemetry_sha256: str
    telemetry_size: int
    telemetry_version: str
    telemetry_file_id: str
    command: CommandIdentity
    deadline_ms: int
    grace_ms: int


# A production policy requires an independently reviewed, exact manifest.
PRODUCTION_POLICIES: dict[str, Policy] = {}
REQUIRED_ENV = frozenset(name.casefold() for name in (
    "PATH", "INCLUDE", "LIB", "LIBPATH", "CL", "_CL_", "LINK", "_LINK_",
    "SystemRoot", "TMP", "TEMP"))


def _digest(value: str) -> bool:
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{64}", value) is not None


def _absolute(path: str) -> bool:
    return isinstance(path, str) and bool(ntpath.splitdrive(path)[0]) and ntpath.isabs(path)


def _canonical(path: str) -> str:
    return ntpath.normcase(ntpath.normpath(path))


def environment_digest(names: tuple[str, ...], environment) -> str:
    if (not isinstance(names, tuple) or not all(isinstance(n, str) and n for n in names)
            or tuple(sorted(names, key=str.casefold)) != names
            or len({name.casefold() for name in names}) != len(names)):
        raise ValueError("environment names must be unique and sorted")
    folded = {}
    for key, value in environment.items():
        if not isinstance(key, str) or not isinstance(value, str) or key.casefold() in folded:
            raise ValueError("invalid environment mapping")
        folded[key.casefold()] = value
    selected = [(name, folded.get(name.casefold())) for name in names]
    encoded = json.dumps(selected, ensure_ascii=True, separators=(",", ":")).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


def policy_hash(policy: Policy) -> str:
    from dataclasses import asdict
    encoded = json.dumps(asdict(policy), sort_keys=True, ensure_ascii=True,
                         separators=(",", ":")).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


def validate_command(policy: Policy, command: CommandIdentity) -> CommandIdentity:
    if policy.kind not in ("production", "fixture"):
        raise ValueError("unknown policy kind")
    if policy.kind == "production" and PRODUCTION_POLICIES.get(policy.identity) != policy:
        raise ValueError("production policy is not registered")
    if (not policy.identity or type(policy.deadline_ms) is not int
            or type(policy.grace_ms) is not int or policy.deadline_ms <= 0
            or not 0 <= policy.grace_ms <= min(1000, policy.deadline_ms)):
        raise ValueError("invalid policy deadline")
    if command != policy.command:
        raise ValueError("command differs from reviewed manifest")
    for digest in (policy.source_sha256, policy.telemetry_sha256, command.executable_sha256,
                   command.response_sha256, command.env_digest):
        if not _digest(digest):
            raise ValueError("invalid digest")
    if type(command.executable_size) is not int or command.executable_size <= 0:
        raise ValueError("invalid executable size")
    if (not _absolute(policy.telemetry_path) or type(policy.telemetry_size) is not int
            or policy.telemetry_size <= 0 or not policy.telemetry_version or not policy.telemetry_file_id):
        raise ValueError("invalid telemetry file pin")
    if not isinstance(command.args, tuple) or not all(isinstance(a, str) for a in command.args):
        raise ValueError("invalid arguments")
    paths = (command.executable, command.cwd, command.stdout, command.stderr,
             command.response_path, *command.outputs)
    if not isinstance(command.outputs, tuple) or not command.outputs or not all(map(_absolute, paths)):
        raise ValueError("absolute paths and outputs required")
    leaves = (command.executable, command.response_path, command.stdout,
              command.stderr, *command.outputs)
    if len({_canonical(path) for path in leaves}) != len(leaves):
        raise ValueError("input and output paths overlap")
    if (not isinstance(command.env_names, tuple)
            or not all(isinstance(n, str) and n for n in command.env_names)
            or tuple(sorted(command.env_names, key=str.casefold)) != command.env_names
            or len({n.casefold() for n in command.env_names}) != len(command.env_names)):
        raise ValueError("invalid environment names")
    if policy.kind == "production" and not REQUIRED_ENV.issubset(
            {name.casefold() for name in command.env_names}):
        raise ValueError("production environment coverage incomplete")
    if policy.kind == "fixture":
        if policy.command_form != "fixture":
            raise ValueError("invalid fixture form")
    elif policy.command_form == "cl_response":
        if command.args != ("@" + command.response_path,):
            raise ValueError("invalid compiler arguments")
    elif policy.command_form == "cmake_vs_link":
        if len(command.args) < 3 or command.args[:2] != ("-E", "vs_link_exe"):
            raise ValueError("invalid linker arguments")
    else:
        raise ValueError("invalid production command form")
    return command


def _file_hash(path: str) -> tuple[str, int]:
    digest = hashlib.sha256()
    size = 0
    with open(path, "rb") as source:
        while True:
            chunk = source.read(1024 * 1024)
            if not chunk:
                break
            size += len(chunk)
            digest.update(chunk)
    return digest.hexdigest(), size


def inspect_command_inputs(policy: Policy, environment) -> CommandIdentity:
    command = validate_command(policy, policy.command)
    if policy.kind == "production" and not {"path", "systemroot"}.issubset(
            {key.casefold() for key in environment}):
        raise ValueError("required environment missing")
    if _file_hash(command.executable) != (command.executable_sha256, command.executable_size):
        raise ValueError("executable identity mismatch")
    if _file_hash(command.response_path)[0] != command.response_sha256:
        raise ValueError("response identity mismatch")
    if environment_digest(command.env_names, environment) != command.env_digest:
        raise ValueError("environment identity mismatch")
    if any(os.path.lexists(path) for path in (command.stdout, command.stderr, *command.outputs)):
        raise ValueError("output path is not fresh")
    return command
