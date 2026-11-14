# Offline provider configure: source proposal, not enrolled

The two overlays replace exact provider files only after their full bytes have
been verified. They remove the floating Rust installation/update and the
configure-time ExternalProject download, extraction, and patch. They are
source proposals, not an executable producer or an authorized configure.

| Original path | Git blob | SHA-256 |
| --- | --- | --- |
| toolchain/rustup.cmake | 088936428c2a9a552bb9f2dd8969ba70b3bd9f7a | 5ec4d394c320716e66215f0c83d62733e603a2e5d2e97e2bed7ebc427e8cf57f |
| cmake/download_externalproject.cmake | 0725925d8af8fe1c6f38cda40d8e6c36944d3a72 | cb384f2032ea35ef487fdae7e32bdcdec9b48464df0ea8c54b3ac3c0b7ea9f88 |

The original root CMakeLists executes the host compiler, Meson and a compiler
feature probe at configure time. Provider helper code also executes processes;
the staged patched ExternalProject module itself has not been fully audited.
ExternalProject recipes still contain network source and update commands,
floating Git branches, and implicit Cargo fetches. scripts/autobuild.sh runs
`ninja update`. These overlays do not neutralize those sites. In particular,
the CMake existence checks are not authentication of staged bytes.

Do not run configure, build, or either overlay. A complete input lock and
source closure, a verified transformation of all download/update sites, an
offline Cargo dependency store, a pinned staged ExternalProject module and
its include graph, and a reviewed execution boundary remain required. The
lock must bind the provider and mpv revisions, bridge patches, GCC 14.4.0,
Rust nightly-2026-10-11 host and Windows GNU target, source trees, submodules,
archives, host tools, and all transitive fetches. Baseline and v3 must use
one lock with separate trees. Feature parity and artifact production remain
unverified. No configure-only command is proposed as safe until these gaps
are closed and reviewed.
