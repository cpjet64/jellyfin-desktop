# Tahoe library formula audit D: pcre2 through zstd

Static inspection at core `c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5`,
stable macOS Tahoe x86_64. Sources and dependency edges are in the separate
[source](macos-library-sources.md) and
[helper-excluded graph](macos-media-binary-helper-graph.md) indexes. No source
build or conditional evaluation was executed. “Standard args” below are
Homebrew helper expansions and remain unresolved for the custom prefix.

| Formula | Tahoe stable build selection and relevant flags |
| --- | --- |
| pcre2 | macOS bzip2/libedit; configure 16- and 32-bit, pcre2grep zlib/bzip2, JIT and pcre2test libedit. HEAD-only autogen is excluded. |
| pixman | Meson `--default-library=both`; build/install. |
| pkgconf | Existing host 3.0.6 is a proposed external tool, not reference 3.0.10. Muon is HEAD-only; if source-built stable, configure with system SDK include `/usr/include`, system lib `/usr/lib`, explicit pkgconfig directories and silent rules disabled. |
| python-setuptools | Build/install through Python pip with Homebrew pip arguments, then generate versioned Python site-package symlinks and rewrite `/usr/local` to the prefix. Isolated prefix adaptation and offline backend are unresolved. |
| python@3.15 | Configure IPv6, SQLite loadable extensions, prefix OpenSSL, optimizations, system expat/mpdecimal, editline, LTO, macOS framework, dtrace and ndbm. The clang build >=1700 tail-call condition applies to the observed clang build 2100.1.1.101. Formula targets host macOS version, applies local sysconfig patch, builds flit-core/pip wheels and installs them offline. Replace Homebrew path and framework rewrites with tested job-prefix equivalents. |
| readline | Six numbered, ascending `-p0` patches in [remote patch list](macos-library-remote-patches.md); configure `--with-curses`, install with `SHLIB_LIBS=-lcurses`; macOS ncurses. |
| rubberband | Meson `-Dresampler=libsamplerate`; Linux-only FFTW flag excluded. |
| sdl2-compat | macOS CMake RPATH to SDL3; configure with Homebrew global install prefix but install with component prefix, then symlink `sdl2.pc`. A job-owned equivalent and runtime SDL3 resolution need proof. |
| sdl3 | CMake `-DSDL_TESTS=OFF -DSDL_X11_XTEST=OFF`; rewrite `sdl3.pc.in` prefix, build/install. |
| shaderc | Stage three pinned Git resources; edit third-party CMake install settings as described in [mpv input notes](macos-library-mpv-lock.md); disable shaderc tests and glslang/SPIRV-Tools/googletest installs. |
| sqlite | CPPFLAGS enable API armor, metadata, dbstat, FTS3/FTS5, geopoly, JSON1, memory management, rtree, stat4, unlock-notify, URI and 250000 variables. Configure readline and session, disable editline, point to selected readline. Serialize make/install; rewrite sqlite3.pc prefix. Linux-only legacy soname excluded. |
| svt-av1 | CMake install RPATH; Intel NASM required. |
| uchardet | CMake install RPATH and `-DCMAKE_POLICY_VERSION_MINIMUM=3.5`. |
| util-macros | Configure `--disable-silent-rules`; install. |
| vapoursynth | Stage pinned glslang resource and overlay its packagefiles, then use Python virtualenv with resources except glslang; install prefix-local `.pth`, plugin and pkgconfig links. Ventura-or-older LLVM LDFLAGS branch is excluded on Tahoe. Python 3.15 runtime and build backend must be pinned and isolated. |
| vulkan-headers | CMake install selected headers and registry. |
| vulkan-loader | CMake points to selected header prefix/include and sysconfdir. Formula fallback paths include system and Homebrew directories; these need deliberate isolated-prefix replacements and runtime verification. |
| webp | CMake builds shared and static variants with `-DCMAKE_DISABLE_FIND_PACKAGE_TIFF=ON`, specified RPATH, shared ON then OFF. Excluding TIFF prevents the libtiff/webp dependency cycle. |
| x264 | Intel NASM; configure disable lsmash, swscale and ffms, enable shared, static and strip. Git commit is pinned but proposed archive byte hash remains unknown. |
| x265 | Build 10-bit with HDR10+ and 12-bit with `MAIN12=ON`, both high-bit-depth, C API export/shared/CLI disabled; build 8-bit linked to 10/12-bit archives, combine with macOS `libtool -static`, then install. Linux ARM SVE2 condition is excluded. |
| xcb-proto | Configure explicit prefix, sysconfdir, localstatedir and Python with silent rules disabled; Python 3.15 build/test input. |
| xorgproto | Configure explicit sysconfdir and localstatedir and disable silent rules. |
| xtrans | Configure explicit sysconfdir and localstatedir, disable silent rules and `--enable-docs=no`. |
| xz | Configure silent rules and NLS disabled; run upstream `make check` then install. |
| zimg | Run autogen using autoconf, automake and libtool, configure explicit prefix, then install. |
| zstd | CMake shared, programs linked shared, contrib and legacy enabled; enable zlib/lzma/lz4, C++11 and install RPATH. Rewrite `libzstd.pc` prefix after installation. |

The graph also treats `cmake`, `ninja` and `pkgconf` as observed external
tools; no formula rebuild is implied. Each source or resource remains subject
to its recorded digest, offline acquisition and artifact gates. This audit
does not resolve actual standard arguments, environment shims, host SDK
libraries, minimum deployment target, Homebrew path rewrites, run-time feature
tests or installation relocatability.
