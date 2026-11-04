# Intel Tahoe media-library proposal

This is a **review proposal with explicit blockers**, not a build authorization
or an executable lock. The separate official helper is being qualified under
its own work; its Deno, Rust and LLVM build trees are outside this library
proposal. The selected reference is Homebrew core commit
`c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5`, not an automatically approved lock.

## Inputs and boundaries

The observed host is x86_64 Darwin 25.6.0 with selected SDK 26.5 and Apple
clang 21; C++ and Objective-C++ Foundation link probes passed. CMake 4.4.3,
Ninja 1.13.2, pkg-config 3.0.6 and Python 3.13.15 were observed. These are
external host build-tool candidates, not formula-version equivalence; their
identities and suitability for each generator must be checked before use.
Meson 1.12.1 and NASM 3.02 are source inputs. Python 3.15 remains in the
library graph for VapourSynth and the gobject scanner; host Python 3.13 must
not silently replace it. The full Xcode requirement for MoltenVK is not
established by the compiler probe. The deployment target is not selected.
Qt 6.9.3 is a separate job-owned input and not part of this closure.

Use one new job-owned root with distinct `sources/`, `build/`, `prefix/`,
`tmp/` and `evidence/` directories. Proposed maximum compile parallelism is
two processes, with one dependency build at a time. Bind `SDKROOT`, compilers,
architecture and deployment target explicitly; constrain pkg-config, CMake,
linker and executable paths to that prefix, approved host tools and the SDK.
Disable Meson wraps and subproject downloads, pip indexes and all other implicit
network acquisition. Missing required features or an unpinned input is a hard
failure. Do not alter the host's global tools, configuration or services.

## Source, patch and feature selections

[Primary source identities](macos-library-sources.md),
[generator and resource identities](macos-library-resources.md),
[remote patches](macos-library-remote-patches.md),
[local patch metadata](macos-media-local-patches.md),
[mpv inputs](macos-library-mpv-lock.md) and
[FFmpeg inputs](macos-library-ffmpeg-lock.md) record the discovered inputs.
The local patch index includes candidates outside the helper-substituted graph;
do not apply a patch merely because it appears there. The source and resource
tables still need a formula-by-formula conditional review before becoming a
complete lock. Ten selected Git archives have no published archive hash in the
formula; their bounded acquisition is described separately and remains held.

Retain mpv's libmpv, JavaScript, LuaJIT, libarchive, uchardet and Vulkan
features; FFmpeg shared libraries, VideoToolbox, AudioToolbox, OpenSSL,
declared codec libraries and AC3 encoder/filter; libass CoreText, libplacebo
lcms/shaderc/Vulkan and the macOS audio-device-change fix. The sole proposed
formula feature difference is disabling HTML documentation and omitting the
docutils generator; this has not yet been reviewed as behavior-neutral. Keep
all other runtime behavior and the separate helper available. The detailed
[feature matrix](macos-media-feature-matrix.md) specifies acceptance evidence;
autodetection that silently omits a required feature must fail the build.

## Proposed order and special stages

The [dependency-first candidate order](macos-media-binary-helper-graph.md)
provides a fixed review sequence, with host tool entries treated as boundaries.
It is not executable until its conditions and exact command options are
validated. In particular, dependency names alone conceal this required stage:

1. Build and install the prerequisites for GLib into the job-owned prefix.
2. Build a private staging GLib with `-Dintrospection=disabled`; stage its
   pinned gobject-introspection source, build that against staging with
   `-Dcairo=disabled -Ddoctool=disabled -Dtests=false`.
3. Build final GLib with `--default-library=both -Dintrospection=enabled`,
   the formula's other flags and prefix-local paths. Build Cairo and the
   separate gobject-introspection package afterward, then HarfBuzz and libass.
   FreeType explicitly uses `--without-harfbuzz` to avoid a bootstrap cycle.
4. Build the codec, subtitle, rendering and other runtime libraries in the
   reviewed sequence, FFmpeg after its codec inputs, and mpv last. The
   [tool/font](macos-library-tools-lock.md),
   [FFmpeg](macos-library-ffmpeg-lock.md) and
   [mpv](macos-library-mpv-lock.md) notes give formula-derived settings.

The candidate option index records literal formula install lines, including
inactive branches; it is not a command specification. The custom prefix must
replace Homebrew-specific paths and shims with explicit equivalent options,
particularly Python frameworks, libplacebo resource staging and Vulkan registry,
MoltenVK Xcode projects, `mpv.pc`, trust stores and runtime search paths. Review
all derived commands and their source and patch inputs before execution.

## Capacity and proof gates

The observed host free space was 43.4 GB. Compressed-source HEAD observations
are in [the size record](macos-library-source-sizes.md); many are unknown or
unverified. Expanded sources, staged build and temporary data, installed prefix,
evidence and overall peak are all **unknown**. No capacity claim or approval to
download follows. Measure or justify bounds for each category and reserve
rollback headroom before acquisition; stop on any storage error.

After a separately authorized build, verify hashes, x86_64 slices and minimum
OS for all outputs, install names, RPATHs and recursive non-system dylibs;
prove headers, pkg-config and CMake discovery use the intended prefix and SDK.
Run feature checks including libavfilter AC3 and TLS failure behavior, then
exercise embedded Desktop video, audio-device change, subtitles and playback.
A build-only pass is distinct from native GUI and hands-on acceptance.
