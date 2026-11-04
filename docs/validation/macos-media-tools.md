# macOS media toolchain baseline

This is a source-metadata assessment for x86_64 macOS Tahoe, not an installed
dependency lock or a successful build. The comparison baseline is Homebrew
core commit `c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5`; the
[pinned formula tree](https://github.com/Homebrew/homebrew-core/tree/c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5/Formula)
is the public reference. Formula source,
patches, resources, build tools, SDK and actual output all need independent
identity and closure verification before a reproducible build can be claimed.

## Media features and tool dependencies

| Formula source baseline | Relevant declared configuration |
| --- | --- |
| mpv 0.41.0, revision 12 | Meson enables libmpv, JavaScript, LuaJIT, libarchive, uchardet and Vulkan. Depends on FFmpeg, libass, libbluray, libplacebo, lcms2, rubberband, VapourSynth, zimg and macOS MoltenVK among others. Includes an audio-device-change use-after-free backport for macOS 26/27 and a VapourSynth compatibility backport. |
| FFmpeg 9.0.2, revision 1 | Shared libraries, GPL and version 3, VideoToolbox and AudioToolbox on macOS; explicitly enables dav1d, libvpx, Opus, LAME, x264, x265, SVT-AV1, VMAF and OpenSSL. |
| libass 0.17.5, revision 1 | FreeType, FriBidi, HarfBuzz and libunibreak; fontconfig disabled on macOS. |
| libplacebo 7.387.0 | Meson enables lcms, shaderc and Vulkan. Build resources are glad2 2.0.8, Jinja2 3.1.6 and MarkupSafe 3.0.4. |
| VapourSynth 82 | Depends on Python 3.15 and zimg; has a glslang `vulkan-sdk-1.4.363.0` source resource. |

These declarations do not establish which features were detected, linked or
usable in a particular binary. In particular, a Vulkan dependency does not
prove that Desktop's embedded OpenGL render path uses Vulkan. Confirm actual
configuration and dynamic-library closure on the target and exercise the app.

The declared build graph includes Meson 1.12.1 (which depends on Ninja and
Python 3.15), Ninja 1.13.2 and pkgconf 3.0.10. Python is also a build dependency
of libplacebo and a runtime dependency of VapourSynth. Python 3.15.2's formula
contains flit-core 4.1.0 and pip 26.2.2 resources. libplacebo's three resources
and VapourSynth's glslang resource are separate pinned inputs, not covered by
the top-level source version alone. Their URLs and SHA-256 values are in the
captured formula text; a complete lock must enumerate and verify each one.

## yt-dlp and its build graph

The mpv formula declares yt-dlp. The pinned yt-dlp formula selects version
2026.10.26 and declares Deno 2.9.8,
Python 3.15, certifi, cffi and pycparser, plus these source resources: Brotli
1.2.1, charset-normalizer 3.5.2, curl-cffi 0.17.0, idna 3.20, mutagen 1.48.1,
pycryptodomex 3.24.1, requests 2.34.2, urllib3 2.8.0, websockets 17.3 and
yt-dlp-ejs 0.8.0. Deno's formula builds with Rust, LLVM, LLD, CMake, Ninja and
pkgconf, invokes Cargo with `--no-default-features`, and requires Xcode 15 or
later on macOS. The Rust formula identifies Rust 1.99.1 source and Rust 1.98.0
bootstrap components, including x86_64 Apple binaries. These names and versions
are not a complete Cargo, V8, Python or bootstrap source lock; audit each
formula's install logic, nested inputs, checksums and network behavior.

The upstream [Deno v2.9.8 Cargo.lock](https://raw.githubusercontent.com/denoland/deno/v2.9.8/Cargo.lock)
was read as metadata, not as a source archive: 295,518 bytes, SHA-256
`36aab78913808a9fb2e5a3a3a27a74a1a1045011133f28beaac6eabf57fc9895`.
It declares 1,130 package records and 1,048 registry checksums. The lock is a
conservative source-input inventory, not a determination of which packages the
Tahoe build needs; nor does this observation prove the release `deno_src.tar.gz`
contains those exact bytes or all offline V8/GN build inputs. The pinned Deno
formula invokes Cargo and enables V8, so a finite offline package/vendor and V8
resource manifest must be resolved before a no-fallback build can be approved.
The ten archive digest proposals elsewhere do not cover those inputs.

At Desktop integration commit `f9cd456`, the player requests `ytdl=false` without
checking the return value, then applies user-supplied manual mpv properties.
The effective default is unverified. Excluding yt-dlp may
be reasonable for the default Desktop playback scope only; it changes the
declared Homebrew dependency closure and leaves custom yt-dlp use unproven.
Document that scope and do not represent it as full formula parity. Conversely,
retaining it requires its executable, resources and supported runtime closure,
not just a dependency name.

## Tahoe and acceptance gaps

The captured formulas list arm64 Tahoe bottles for these tools, but no x86_64
Tahoe bottle for mpv, yt-dlp, Deno, Rust, Python 3.15, Ninja, pkgconf,
VapourSynth or libplacebo. This is a statement about those captured bottle
blocks, not proof that source builds are impossible or that every transitive
dependency lacks an Intel bottle. A source-build plan must account for the
Intel SDK, compiler, architecture-specific bootstrap, build resources, network
inputs, disk headroom, and any upstream architecture support before installation.
Meson's formula lists an architecture-independent bottle; its Python dependency
still needs a target-compatible resolution.

Verify hashes of every selected source, patch, resource and bootstrap input;
record exact compiler, SDK, flags and transitive tools; prevent undeclared
downloads; and inspect the resulting library paths and feature configuration.
Then validate Desktop embedded rendering and responsiveness, audio device
changes, normal and hardware decoding, subtitles and font shaping, seeking,
track selection and playback lifecycle. S/PDIF plus AC3 passthrough enables
the `lavcac3enc` audio filter in Desktop; retain and test that conditional path
with suitable hardware rather than assuming a basic decode smoke covers it.
Standalone mpv tests cannot establish embedded rendering or receiver behavior.
