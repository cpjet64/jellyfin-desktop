"""Guarded producer proposal; independently review before any execution."""

import hashlib
import json
import os
from pathlib import Path
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
                "dev/ci/recovery_mpv_producer/commands.py",
                "dev/ci/recovery_mpv_producer/supervisor.py",
                "dev/ci/recovery_mpv_producer/stage.py",
                "dev/ci/recovery_mpv_producer/plan.py",
                "patches/mpv/build/offline_provider/adapter.py",
                "dev/ci/portable_proof/executor.py"}
    if not required.issubset(entries):
        raise ValueError("review manifest omits executable source")


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
    inventory = verified_json(context.inventory, context.inventory_sha256)
    input_gate.verify_inventory(lock, inputs, inventory)
    if lock["provider_commit"] != PROVIDER or lock["mpv_commit"] != MPV:
        raise ValueError("source selection mismatch")
    for tool in ("cmake", "ninja", "git"):
        verify_tool(getattr(context, tool), review["tools"][tool])
    for name, expected in PATCHES:
        verify_tool(root / "patches/mpv" / name, expected)
    # The independently reviewed manifest must bind a complete extracted tree
    # inventory and exact Git-object validation before this function is enabled.
    if not context.verify_source_objects(lock, inventory, review):
        raise ValueError("source object and extracted-tree identities unconfirmed")
    adapter.plan(context.provider_source, inputs, context.adapter_lock, context.overlays)
    output.mkdir()
    (output / "sources").mkdir()
    adapter.apply(context.provider_source, inputs, context.adapter_lock,
                  context.overlays, output / "sources/provider")
    context.copy_verified_mpv(output / "sources/mpv")
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
