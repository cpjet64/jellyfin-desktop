"""Guarded invocation proposal; intentionally disabled until independently reviewed."""

import hashlib
import os
from pathlib import Path

from commands import child_environment, commands
from stage import stage_sdk

# A reviewed integration must replace this source constant after qualifying the
# adapter, closed input graph, immutable tools and cgroup/network supervisor.
REVIEWED_INTEGRATION = False


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


def produce(context, supervisor):
    """Invoke only fixed commands through a qualified, leased Linux supervisor.

    The supervisor must verify a unique task-owned cgroup lease and deny network,
    run the supplied argv and environment, enforce phase and overall deadlines,
    retain bounded logs, kill only its own cgroup, and confirm it is empty.
    This proposed interface has not been integrated or qualified.
    """
    if not REVIEWED_INTEGRATION:
        raise RuntimeError("producer integration is not reviewed or enabled")
    if os.name != "posix" or not context.prerequisites_verified:
        raise RuntimeError("Linux and verified complete inputs required")
    if not supervisor.confirm_lease(context.lease_identity, deny_network=True):
        raise RuntimeError("owned cgroup and network-denial lease unconfirmed")
    verify_tool(context.cmake, context.cmake_sha256)
    verify_tool(context.ninja, context.ninja_sha256)
    env = child_environment(context.workspace, context.tool_bin)
    for command in commands(context.workspace, context.cmake, context.ninja):
        result = supervisor.run_fixed(command, env, context.overall_deadline)
        if result.exit_code != 0 or not result.cgroup_empty or not result.log_complete:
            raise RuntimeError("phase failed or containment incomplete: " + command.phase)
    return stage_sdk(context.workspace, context.identities)
