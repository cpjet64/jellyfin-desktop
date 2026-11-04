# Media graph discovery, Intel macOS stable branches

This is a static formula declaration walk, not a complete evaluated lock.
The host probe selected SDK 26.5; the build deployment target remains open, and macOS version changes
conditional edges. The graph below assumes Sequoia or newer for its system
Python expat selection. It excludes HEAD, Linux, ARM and test-only branches.
System-provided libraries and tools still require independent SDK/host checks.

FFmpeg's stable declaration edges (`B` means build-time):

- ffmpeg → pkgconf B, nasm B, dav1d, lame, libvmaf, libvpx, openssl@4,
  opus, sdl2-compat, svt-av1, x264, x265, xz.
- dav1d and libvmaf → meson B, ninja B, nasm B; libvpx and x264 → nasm B;
  svt-av1 and x265 → cmake B, nasm B.
- lame → pkgconf B, mpg123; openssl@4 → ca-certificates (no linkage);
  sdl2-compat → cmake B, sdl3 (no linkage); sdl3 → cmake B, pkgconf B.
- meson → ninja, python@3.15; python@3.15 → pkgconf B, ca-certificates
  (no linkage), mpdecimal, openssl@4, sqlite, xz, zstd; sqlite → readline;
  zstd → cmake B, lz4, xz; lz4 → cmake B.

The declared reference uses macOS bzip2 and libxml2 in FFmpeg, and additional
system tools/libraries in its build dependencies. These must be bound to the
chosen SDK/toolchain rather than discovered from arbitrary host prefixes.
The core Python formula refers to `Patches/python/3.13-sysconfig.diff` in the
same pinned core tree; its byte hash is in the acquisition note. Python also has flit-core and pip
resources; readline has multiple patches. Exact source, resource and patch
locks and build options for the graph are not complete.

The mpv stable Intel graph contains a much wider family, including libass,
libplacebo, MoltenVK, LuaJIT, mujs, VapourSynth, yt-dlp and their generators.
A filtered static walk identified 93 candidate formulas besides mpv/FFmpeg;
that is a candidate set, not proof of the selected closure. Notable conditions:
VapourSynth adds LLVM on Ventura or earlier; LLVM selects z3 on Sonoma or newer;
yt-dlp's stable Deno dependency adds LLVM, Rust and Xcode requirements; Cairo
declares X11 libraries on macOS; cffi's build/test requirements name Python
3.14 and 3.15. Rust's Intel bootstrap includes prebuilt rustc, cargo and
rust-std archives. Stable shaderc carries three additional Git resources beyond
the six MoltenVK resources. The proposed byte-acquisition list includes their
specific revisions, but its caps are not a complete build-storage estimate.

Homebrew formula declarations are not a substitute for a build plan: shared
dependency cycles, bootstrap choices, resource generation, installation
options and any deliberate feature exclusions need explicit resolution. The
full ordered graph, hashes, patch provenance, actual disk peak and verified
toolchain are not yet available; do not start a source build on this evidence.

The intended proposal retains yt-dlp, including its custom-setting behavior;
the only proposed omission at this point is mpv HTML documentation generation,
which would change `-Dhtml-build=enabled` to disabled and omit docutils. The
normal CI's unpinned `brew install mpv` is a behavior reference, not proof of a
particular host artifact or source build. The [source metadata index](macos-media-source-index.md)
is a conservative static list, not a verified dependency closure. Its first
source and SHA values do not capture all resources, patches or conditionals.

The separate bounded host probe reports Darwin 25.6.0 x86_64, SDK 26.5,
Apple clang 21, CMake/CTest 4.4.3, Ninja 1.13.2, Python 3.13.15 and successful
x86_64 C++ and Objective-C++ Foundation link probes. Suitability of Python
3.13 as the Meson and other generators' interpreter remains to be checked;
VapourSynth's Python runtime cannot be silently replaced with an unverified
host interpreter. The inspected host had 43.4 GB free; no build peak has been
calculated. Tool versions and compilation of a probe are not media-build proof.
