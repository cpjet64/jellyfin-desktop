"""Guarded producer proposal; independently review before any execution."""

import hashlib
import json
import os
from pathlib import Path
import shutil
import time

from commands import Command, child_environment, commands
from plan import PATCHES, PROVIDER, MPV, validate_layout
from stage import stage_sdk
import supervisor


def verify_tool(path, expected):
    path = Path(path)
    if not path.is_absolute() or path.is_symlink() or not path.is_file():
        raise ValueError("tool must be an absolute regular non-symlink file")
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(chunk)
    if sha.hexdigest() != expected:
        raise ValueError("tool hash mismatch")


def verified_json(path, expected):
    data = Path(path).read_bytes()
    if len(expected) != 64 or hashlib.sha256(data).hexdigest() != expected:
        raise ValueError("reviewed manifest digest mismatch")
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("duplicate manifest key")
            result[key] = value
        return result
    return json.loads(data, object_pairs_hook=unique)


def check_review(review, root, source_sha):
    if review.get("reviewed_source_sha") != source_sha or len(source_sha) != 40:
        raise ValueError("reviewed source identity mismatch")
    entries = review.get("files")
    if not isinstance(entries, dict) or not entries:
        raise ValueError("reviewed source file identities missing")
    for relative, expected in entries.items():
        path = Path(relative)
        if path.is_absolute() or ".." in path.parts or not path.parts:
            raise ValueError("invalid reviewed file path")
        verify_tool(root / path, expected)
    required = {"dev/ci/recovery_mpv_producer/invoke.py",
                "dev/ci/recovery_mpv_producer/bootstrap.py",
                "dev/ci/recovery_mpv_producer/commands.py",
                "dev/ci/recovery_mpv_producer/supervisor.py",
                "dev/ci/recovery_mpv_producer/stage.py",
                "dev/ci/recovery_mpv_producer/plan.py",
                "patches/mpv/build/offline_provider/adapter.py",
                "patches/mpv/build/offline_inputs.py",
                "patches/mpv/build/offline_git.py",
                "patches/mpv/build/recovery_lock_preflight.py",
                "patches/mpv/build/subrandr-registry-inventory.json",
                "dev/ci/portable_proof/assembly.py",
                "dev/ci/portable_proof/executor.py"}
    if not required.issubset(entries):
        raise ValueError("review manifest omits executable source")


def copy_and_verify(context, lock, inventory, output, input_gate):
    """Copy fixed source roots and compare copies against their locked Git objects."""
    paths = {}
    for name in ("provider", "mpv"):
        source = Path(context.provider_source if name == "provider" else context.mpv_source)
        destination = output / "sources" / (name + "-verified")
        shutil.copytree(source, destination, symlinks=True)
        input_gate.verify_source_checkout(lock, context.inputs, inventory, context.git,
                                          output / "checks" / ("checkout-" + name),
                                          name, destination)
        paths[name] = destination
    return paths


def produce(context, lock_gate, input_gate, adapter, executor):
    """Validate approved inputs then run only fixed phases in an owned session.

    Module objects must be loaded from the reviewed source tree by the caller;
    this interface and its bootstrap require independent review before execution.
    """
    if os.uname().sysname != "Linux" or os.uname().machine != "x86_64":
        raise ValueError("native Linux x86_64 required")
    root, inputs, output = validate_layout(context.root, context.inputs, context.output)
    review = verified_json(context.review_manifest, context.review_sha256)
    check_review(review, root, context.reviewed_source_sha)
    lock = lock_gate.verify(context.lock, context.lock_sha256)
    verification = lock.get("verification", {})
    if (verification.get("provider_adapter_complete") is not True or
            verification.get("cargo_nested_offline_proven") is not True):
        raise ValueError("provider offline and nested Cargo evidence incomplete")
    inventory = verified_json(context.inventory, context.inventory_sha256)
    if lock["provider_commit"] != PROVIDER or lock["mpv_commit"] != MPV:
        raise ValueError("source selection mismatch")
    for tool in ("cmake", "ninja", "git"):
        verify_tool(getattr(context, tool), lock["host_tools"][tool]["sha256"])
    for name, expected in PATCHES:
        verify_tool(root / "patches/mpv" / name, expected)
    if not output.is_relative_to(root / "deps"):
        raise ValueError("task workspace must be under the reviewed root deps directory")
    output.mkdir()
    (output / "checks").mkdir(mode=0o700)
    input_gate.verify_inventory(lock, inputs, inventory, context.git, output / "checks")
    (output / "sources").mkdir()
    sources = copy_and_verify(context, lock, inventory, output, input_gate)
    adapter.plan(sources["provider"], inputs, lock["provider_adapter"], context.overlays)
    adapter.apply(sources["provider"], inputs, lock["provider_adapter"],
                  context.overlays, output / "sources/provider")
    sources["mpv"].rename(output / "sources/mpv")
    for name in ("tmp", "home", "cargo-home", "logs"):
        (output / name).mkdir()
    env = child_environment(output, context.tool_bin)
    deadline = time.monotonic() + 33000
    for index, (name, _) in enumerate(PATCHES):
        command = Command("patch-" + str(index),
                          (str(context.git), "apply", "--whitespace=nowarn",
                           str(root / "patches/mpv" / name)), output / "sources/mpv", 300)
        supervisor.run(command, env, output / "logs" / (command.phase + ".log"), deadline, executor)
    for command in commands(output, context.cmake, context.ninja):
        supervisor.run(command, env, output / "logs" / (command.phase + ".log"), deadline, executor)
    return stage_sdk(output, {"reviewed_source_sha": context.reviewed_source_sha,
                              "lock_sha256": context.lock_sha256,
                              "inventory_sha256": context.inventory_sha256})
