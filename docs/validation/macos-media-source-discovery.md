# Intel macOS media source discovery — incomplete proposal

This is discovery evidence, not an approved build lock or a recipe to execute.
The requested full source closure, feature-difference matrix, verified storage
budget and toolchain selection remain open. Do not start downloads or builds
from this document. The independent Qt 6.9.3 job-owned prefix is a separate
input. The subsequent toolchain probe selected SDK 26.5 and Apple clang 21 and
passed x86_64 C++ and Objective-C++ link probes. The build deployment target,
full Xcode availability and compatibility with this dependency graph remain open.

## Reference and observed inputs

The feature-discovery reference is Homebrew/homebrew-core commit
`c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5`; its formulae do not themselves
approve a source lock. The [mpv formula](https://github.com/Homebrew/homebrew-core/blob/c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5/Formula/m/mpv.rb)
pins mpv 0.41.0, source
`https://github.com/mpv-player/mpv/archive/refs/tags/v0.41.0.tar.gz`, SHA-256
`ee21092a5ee427353392360929dc64645c54479aefdb5babc5cfbb5fad626209`.
Its macOS audio-device-change use-after-free backport is
`https://github.com/mpv-player/mpv/commit/c5d391adba7bd024954d0df1e0405f5749f4d4ca.patch?full_index=1`,
SHA-256 `769b218df220738cc1cf9f81cf696c16518c5dfe56a5ef028e33b22536e0e924`.
The formula also specifies a VapourSynth compatibility patch; whether that
feature and patch can be omitted requires a documented behavior decision.

The [FFmpeg formula](https://github.com/Homebrew/homebrew-core/blob/c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5/Formula/f/ffmpeg.rb)
pins 9.0.2, source `https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz`, SHA-256
`8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e`.
Its explicit options include shared libraries, GPL and version 3, OpenSSL,
VideoToolbox, AudioToolbox, and external codec libraries. These options have
licensing and feature implications; copying only the version is insufficient.

The mpv formula enables libmpv, LuaJIT, JavaScript, libarchive, uchardet and
Vulkan, and also depends on libbluray, libplacebo, MoltenVK, VapourSynth,
rubberband, yt-dlp and other packages. FFmpeg adds dav1d, LAME, libvmaf,
libvpx, OpenSSL, Opus, SDL, SVT-AV1, x264, x265, xz and x86 NASM. These are
only direct dependencies, not a closure. Build tools, resources, patches,
transitive dependencies, exact options and their provenance must be resolved.

The [libass formula](https://github.com/Homebrew/homebrew-core/blob/c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5/Formula/lib/libass.rb)
pins 0.17.5, source
`https://github.com/libass/libass/releases/download/0.17.5/libass-0.17.5.tar.xz`,
SHA-256 `2dca25c0e0c837ddf00b52011b3f82cac1e4ddd3ad018227806b0c2288864acc`.
It disables fontconfig on macOS and identifies CoreText as the macOS provider.
The [libplacebo formula](https://github.com/Homebrew/homebrew-core/blob/c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5/Formula/lib/libplacebo.rb)
pins 7.387.0, source
`https://code.videolan.org/videolan/libplacebo/-/archive/v7.387.0/libplacebo-v7.387.0.tar.bz2`,
SHA-256 `14ceeeb14d1c08fcdb1a782a06129da11f842c6fddbf3a43c1c4061406ad03d2`.
Its generator resources include glad2, Jinja2 and MarkupSafe. These four
principal sources alone do not constitute the required closure.

At integration commit `f9cd456`, `src/player/MpvVideoItem.cpp` selects
`vo=libmpv`; `src/player/PlayerComponent.cpp` sets hardware decoding, audio
device and passthrough, subtitles, track selection and `loadfile` URLs. User
settings can modify mpv behavior. A standalone CLI smoke test therefore does
not prove embedded Desktop rendering, audible playback or settings behavior.
Excluding yt-dlp or scripting as merely CLI dependencies is not yet justified:
the formula includes them and Desktop accepts media URLs and user settings.

## Specific missing information

The [x264 formula](https://github.com/Homebrew/homebrew-core/blob/c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5/Formula/x/x264.rb)
identifies git revision `b35605ace3ddf7c1a5d67a2eb553f034aef41d55` but does
not declare an archive SHA-256. The
[MoltenVK formula](https://github.com/Homebrew/homebrew-core/blob/c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5/Formula/m/molten-vk.rb)
uses six pinned git resources without archive SHA-256 values and requires
Xcode. A source-byte digest needs trusted published digest evidence or reading
the chosen source archives; the latter is outside the authorized metadata-only
discovery. Full Xcode availability and the build deployment target must also be
established. These are specific missing inputs,
not evidence that the source route is impossible.

The existing [bounded host observation](macos-prerequisites-2026-11-04.md)
did not find the selected media pkg-config packages, Meson or Qt in the probed
locations. It does not prove host-wide absence or C++ compiler absence.

## Acceptance criteria for a future complete proposal

The proposal must pin each source, patch and generated resource by URL, version
or commit and SHA-256; describe dependency edges, build tools, build options,
and build order; identify the SDK, x86_64 deployment target, job-owned paths,
parallelism and network policy; and forbid implicit downloads and fallback
dependencies. It must compare every deliberate feature difference against the
reference, including libmpv rendering, VideoToolbox, audio, ASS with CoreText,
libavfilter AC3, codecs and TLS. Missing optional inputs required by that
matrix must fail configuration or feature verification. CLI and documentation
dependencies can be excluded only with an explicit no-Desktop-behavior-loss
justification.

Evidence must include x86_64 slices, minimum OS, install names and RPATHs,
recursive non-system dylib closure, headers, pkg-config and CMake discovery,
and executable feature tests. Separate source archive, expanded source,
temporary build, installed prefix and evidence sizes are required. All these
sizes and peak storage are currently **unknown**, so no capacity reservation
or safe build approval can be inferred. A build-only result would not prove
native graphics or desktop acceptance.

No source closure or alternative host provisioning specification has been
qualified yet. In particular, installing the current Homebrew formula is not a
known small binary transaction for Intel. A complete lock or a concrete host
alternative needs further discovery and separate review before mutation.
