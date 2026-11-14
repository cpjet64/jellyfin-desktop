"""Bounded offline producer plan. This module performs no build on import."""

from dataclasses import dataclass
from pathlib import Path

PROVIDER = "1f8d2e76255abc48f0456d9be99b4cab9c6566e6"
MPV = "01e2533f3143b8940857ca419b0b367f583e05ca"
RUST_MANIFEST = "b14ac4f9ad835f082cdffd0dc4797c202ae70094a09fa0a6a8100a56e2e17ed3"
PATCHES = (
    ("0001-recovery-frame-observation.patch", "1b44233c501f7cbab66e64269f2d83f0f57b7bc5aceca8975cb218ce36b52edc"),
    ("0002-recovery-seek-identity.patch", "97150ebc3f75adad6c1a4591c0a1c1a168a82d7190cd5d59b03150bf17d292a7"),
    ("0003-recovery-seek-epoch-tests.patch", "6f6221c6aa1a23d8991324d7cad5748a1a0768ad8f318bffb360ca36f8975e7d"),
)
RUST_COMPONENTS = {
    "rustc/x86_64-unknown-linux-gnu": "572aaee68b7f1ca435e61bae5b240576b82a3aa28d1fb1b6f8c2b5017bdc0751",
    "cargo/x86_64-unknown-linux-gnu": "c05795b27e83486e6e5ef9a34cb9818789d114cf4432fbfe74d0601084030d9a",
    "rust-std/x86_64-unknown-linux-gnu": "5fdb55067ee33558d32ac7848227a7c10549b93a6c6edb796ebe83e3b8941956",
    "rust-std/x86_64-pc-windows-gnu": "834cdde42333e0a114fb659b15dd1fce1b0a791ad4e60c135019069d1e344f0e",
}


@dataclass(frozen=True)
class Phase:
    name: str
    output: str
    deadline_seconds: int


PHASES = (
    Phase("validate-identities", "identity-receipt.json", 300),
    Phase("apply-patches", "sources", 300),
    Phase("materialize-toolchains", "toolchains", 1800),
    Phase("configure-baseline", "build/baseline", 1800),
    Phase("build-baseline", "install/baseline", 14400),
    Phase("configure-v3", "build/v3", 1800),
    Phase("build-v3", "install/v3", 14400),
    Phase("stage-raw-sdk", "raw-sdk", 300),
    Phase("record-provenance", "raw-sdk-provenance.json", 300),
)


def validate_layout(workspace, inputs, output):
    """Require separate existing input and fresh output paths in one workspace."""
    workspace = Path(workspace).resolve(strict=True)
    inputs = Path(inputs).resolve(strict=True)
    output = Path(output).resolve(strict=False)
    if not workspace.is_dir() or not inputs.is_dir():
        raise ValueError("workspace and prestaged inputs must be directories")
    if not inputs.is_relative_to(workspace) or not output.is_relative_to(workspace):
        raise ValueError("all producer paths must stay inside the workspace")
    if inputs == workspace or output == workspace or output.exists():
        raise ValueError("inputs must be scoped and output must be fresh")
    if output.is_relative_to(inputs) or inputs.is_relative_to(output):
        raise ValueError("input and output trees must not overlap")
    return workspace, inputs, output


def phase_plan():
    return PHASES
