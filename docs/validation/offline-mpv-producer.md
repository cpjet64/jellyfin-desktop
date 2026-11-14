# Offline MPV producer source checkpoint

This checkpoint is **not an executable build producer**. It records the ordered
phases, fixed source and Rust identities, path layout, fixed CMake configure and
build commands, guarded invocation and raw SDK staging proposal. No configure,
extraction, toolchain installation, build, test, or import of this code was
performed. The proposed supervisor holds its session leader through cleanup,
following `portable_proof/executor.py`, and treats unknown group survivors as
nonpass. It does not establish network isolation or contain a deliberately
escaped child. This proposal has not been qualified for compiler processes.

The intended native Linux VM flow is: validate a separately approved complete
lock and prestaged input inventory, verify the actual Git objects and all
archive hashes; apply the exact reviewed offline provider adaptation and the
three bridge patches; materialize GCC 14.4.0 and the four pinned Rust nightly
components locally; configure and build the provider libmpv profile in separate
baseline and x86-64-v3 trees using one lock; stage GNU DLLs, headers and import
archives; and record hashes and provenance of the raw SDK. The only intended
variant delta is the declared CPU architecture. The provider profile must retain
media, subtitles, scripting, hardware rendering and networking. A separately
approved Windows MSVC import-library and Qt-kit validation is required for a
final artifact; the raw SDK is not that artifact.

Execution requires a complete source and nested-fetch closure, verified host
tools and Cargo vendor inputs, a fixed reviewed provider adapter and its exact
identity, an independently reviewed command sequence, source-object verification,
network isolation, robust owned-process containment and inventory, and a
reviewed staging contract. Meson must use `--wrap-mode=nodownload` and Cargo
must use `--locked --offline`; no update, bootstrap, rustup, branch resolution,
or network access is allowed. Any unrecognized fetch or dependency is a stop.
The selected checkout contains only the first of the three required bridge
patches, and no complete lock or adapter is present. The staging paths and
provider CMake options are proposals that require validation against the fixed
adapter. `CARGO_NET_OFFLINE` and `MESON_WRAP_MODE` alone do not prove that every
nested invocation uses `--locked --offline` and `--wrap-mode=nodownload`; the
adapter and authorized VM isolation must enforce those requirements. Tool
hashes come from the approved complete lock. The verifier integration and
module bootstrap are unexecuted proposals requiring independent review.
The proposed verifier compares task-owned source copies against Git bundle
objects before adaptation. `bootstrap.py` checks the
module bytes before importing them, but its own bytes and the Python interpreter
must be authenticated by the caller before launch. The adapter itself does not
remove every network fetch or materialize the four Rust archives. An authorized
future VM run needs separate review after these gaps are closed.

## Review evidence contract

The proposal requires six distinct `qualified` records: provider and source
graph, provider overlays, nested Cargo, Rust and host toolchains, input lock and
prestage inventory, and reviewed modules. Each binds a SHA-256 subject computed
from the corresponding lock or review-manifest data, plus a reviewed proof path
and hash. The approved review manifest binds the evidence document through
`qualification_evidence_sha256` and must include every proof file identity. The
producer carries that evidence hash into raw SDK provenance. `evidence.py`
defines the proposed format and subject construction. No qualifying document or
proof is supplied by this checkpoint.

Hashes establish identities, not the truth of a qualification. Independent
review must examine the evidence, approve the manifest and source SHA, and
authorize eventual execution. Readiness flags, offline environment variables
and a `qualified` string do not themselves prove closure, overlay correctness,
nested Cargo behavior, or Rust toolchain materialization. Log and output
inventory, exclusive source protection and resulting artifact identities also
require qualification. This is neither a build receipt nor a final Windows kit.
