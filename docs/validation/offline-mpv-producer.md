# Offline MPV producer source checkpoint

This checkpoint is **not an executable build producer**. It records the ordered
phases, fixed source and Rust identities, and path layout. No configure,
extraction, toolchain installation, build, test, or import of this code was
performed. Owned-process supervision remains to be implemented and qualified.

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
patches, and no complete lock or adapter is present. Accordingly there is no
entry point which could configure or build. An authorized future VM run needs
separate review and authorization after these gaps are closed.
