"""Fixed configure and build command proposal for the pinned provider."""

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Command:
    phase: str
    argv: tuple
    cwd: Path
    timeout: int


def commands(workspace, cmake, ninja):
    """Construct the two fixed provider invocations per isolated variant.

    The adapter must preserve provider mpv options and replace all downloads,
    rustup and standalone packaging before this proposal can be qualified.
    """
    workspace, cmake, ninja = map(Path, (workspace, cmake, ninja))
    source = workspace / "sources/provider"
    result = []
    for variant, cpu in (("baseline", "x86-64"), ("v3", "x86-64-v3")):
        build = workspace / "build" / variant
        prefix = workspace / "install" / variant
        configure = (str(cmake), "-S", str(source), "-B", str(build), "-G", "Ninja",
                     "-DCMAKE_MAKE_PROGRAM=" + str(ninja),
                     "-DCMAKE_INSTALL_PREFIX=" + str(prefix),
                     "-DGCC_ARCH=" + cpu)
        result.append(Command("configure-" + variant, configure, workspace, 1800))
        result.append(Command("build-" + variant,
                              (str(cmake), "--build", str(build), "--target", "mpv"),
                              workspace, 14400))
    return tuple(result)


def child_environment(workspace, tool_bin):
    """No inherited environment or online dependency resolution."""
    root, tool_bin = Path(workspace), Path(tool_bin)
    return {"PATH": str(tool_bin), "HOME": str(root / "home"),
            "TMPDIR": str(root / "tmp"), "LC_ALL": "C", "TZ": "UTC",
            "CARGO_HOME": str(root / "cargo-home"), "CARGO_NET_OFFLINE": "true",
            "RUSTUP_TOOLCHAIN": "nightly-2026-10-11",
            "MESON_WRAP_MODE": "nodownload", "GIT_TERMINAL_PROMPT": "0"}
