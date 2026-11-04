# macOS source candidate audit C

This audits the binary-helper graph's alphabetical range `libsndfile` through
`opus` against formula text at Homebrew core commit
[`c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5`](https://github.com/Homebrew/homebrew-core/tree/c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5/Formula),
for stable source on Intel Tahoe. It is metadata review, not archive, patch-byte,
build or runtime verification. Source URLs and SHA-256 values are in the pinned
formulae; exact dependencies beyond this range require their own audit.

| Node and stable source | Intel Tahoe formula findings |
| --- | --- |
| libsndfile 1.2.2 rev 1 | CMake build; FLAC, LAME, Ogg, Vorbis, mpg123, Opus; macOS Python is an additional build input omitted by the graph. Builds shared and static separately, installs shared and copies static archive; programs and package/pkgconfig output enabled, examples disabled, Python executable and install RPATH explicit. |
| libtiff 4.7.2 | JPEG-Turbo, WebP, xz, zstd; zlib-ng is Linux-only. Disables libdeflate and X; enables WebP, zstd and lzma, explicitly locates WebP and JPEG, rewrites pkg-config prefix. |
| libtool 2.6.2 | m4; configure enables ltdl install, disables silent rules, uses `g` program prefix on macOS. |
| libudfread 1.2.0 | Meson and Ninja build tools; default Meson arguments. |
| libunibreak 8.0 | No quoted formula dependencies; configure disables silent rules. |
| libvmaf 3.2.1 | Meson, Ninja, Intel NASM; macOS vim supplies `xxd` as an omitted system build input. Uses `libvmaf` Meson source directory. |
| libvpx 1.17.0 | Intel NASM. Inline `patch :DATA` is a stable build input, including Darwin target support. Enables shared, PIC, runtime CPU detection and VP9 high bit depth; disables examples and unit tests; macOS target derives from CPU and kernel. |
| libx11 1.8.14 | pkgconf, util-macros, xtrans build; libxcb and xorgproto. Configures Unix and TCP transports, IPv6 and X threads; specs disabled. |
| libxau 1.0.12 | pkgconf and util-macros build; xorgproto. Uses explicit sysconf/localstate paths. |
| libxcb 1.17.0 | pkgconf, Python 3.15 and xcb-proto build; libxau and libxdmcp. Python version must match xcb-proto. Enables DRI3, GE, Xevie, Xprint and SELinux options; disables developer docs and Doxygen. Confirm configure results on macOS rather than assuming all options are effective. |
| libxdmcp 1.1.5 | pkgconf build, xorgproto; docs disabled. |
| libxext 1.3.7 | pkgconf build, libx11 and xorgproto; specs disabled. |
| libxrender 0.9.13 | pkgconf build, libx11 and xorgproto; standard configure with sysconf/localstate paths. |
| little-cms2 2.19.1 | JPEG-Turbo and libtiff; standard configure and pkg-config prefix rewrite. |
| lzo 2.10 | No quoted dependencies; configure disables dependency tracking and runs `make check` before install. |
| m4 1.4.21 | No quoted dependencies; configure disables dependency tracking. |
| Meson 1.12.1 | Actual formula depends on Ninja and Python 3.15; graph intentionally stops at this external-tool boundary. Pip install uses build isolation and post-install path rewrites; backend and Python environment need qualification. |
| mpdecimal 4.0.1 | No quoted dependencies; explicit install RPATH flags and standard configure. |
| mpg123 1.33.7 | No quoted dependencies; enables static, uses `.so` module suffix and macOS CoreAudio. CPU-specific configure choice must be resolved and recorded on Intel. |
| mujs 1.3.11 | Linux-only readline and test-only pkgconf are not Tahoe runtime inputs. Stable build applies remote patch `e21c6bfdce374e19800f2455f45828a90fce39da`, SHA-256 `e10de8b9c3a62ffe121b61fe60b67ba8faa68eaace9a3b17a13f46a2cc795a11`, then installs shared output. |
| NASM 3.02 | No stable quoted dependencies; autoconf, automake, asciidoc and xmlto are HEAD-only. Standard configure and install. |
| Ninja 1.13.2 | macOS Python is an omitted build/test system input; bootstraps using `configure.py --bootstrap --verbose --with-python=python3`. Graph treats it as an external tool. |
| OpenSSL 4.1.0 | CA certificates no-linkage; macOS Perl is an omitted build tool. Explicit prefix, openssldir and libdir, plus architecture-specific Configure arguments; certificate installation and trust path require separate verification. |
| Opus 1.6.1 | No stable quoted dependencies; autoconf, automake and libtool are HEAD-only. Configure disables docs. |

The graph's edges are formula declarations, not proof of actual linkage. It
omits `uses_from_macos` tools and deliberately truncates Meson and Ninja, so
these must be supplied and identified in a complete lock. `std_*` arguments,
system SDK libraries, resources and test fixtures, all source and patch byte
hashes, selected architecture flags, install names and generated outputs remain
to be independently verified. Formula test resources are not automatically
required for a production build but must be locked if those tests are selected.
No installation, archive acquisition or build was performed in this audit.

## Additional omitted nodes

The following six nodes were omitted from the initial table and are part of
this audit range:

| Node and stable source | Intel Tahoe formula findings |
| --- | --- |
| libunistring 1.5 | No quoted dependencies. On Sonoma or newer, including Tahoe, formula sets `am_cv_func_iconv_works=yes` as an iconv workaround and skips `make check`; configure disables silent rules. The skipped check is a validation gap. |
| libvorbis 1.3.7 | pkgconf build and libogg runtime. Formula removes `-force_cpusubtype_ALL` from configure. Autoconf, automake and libtool are HEAD-only; `oggfile` is test-only. |
| LuaJIT commit `ea9eb7edb5eac4a9d175d74523db34fac0fa5180` | No quoted dependencies. Rewrites compiler and removes hardcoded `-march`, sets macOS deployment target and FFI RPATH, builds `amalg`, installs symlinks and rewrites module paths in pkg-config. Homebrew prefixes need private-prefix review. |
| lz4 1.10.0 | CMake build dependency. Make installs main output; separate CMake build enables shared and static libraries, disables its CLI, and installs package configuration via staged `DESTDIR`. Rewrites pkg-config prefix. |
| MoltenVK 1.4.2 | macOS only; CMake, Xcode >= 11.7 and macOS Python build inputs. Six stable resources: SPIRV-Cross, SPIRV-Headers, SPIRV-Tools, Vulkan-Headers, Vulkan-Tools and cereal. Stages resources, builds SPIRV-Tools, Xcode external dependencies and macOS package with target architecture; installs frameworks, dylib, converter and rewritten ICD path. Its pre-Sonoma Xcode workaround does not apply on Tahoe. Resource hashes and Xcode/SDK identity require verification. |
| mpv 0.41.0 rev 12 | Meson, Ninja, pkgconf and docutils build inputs; FFmpeg, JPEG-Turbo, libarchive, libass, libbluray, libplacebo, lcms2, LuaJIT, MuJS, rubberband, uchardet, VapourSynth, Vulkan loader, yt-dlp and zimg; MoltenVK on macOS. Linux-only window/audio dependencies and EGL/Wayland/X11 flags do not apply. Enables libmpv, JavaScript, LuaJIT, libarchive, uchardet, Vulkan and HTML docs; disables build date. Two stable backports require pinned bytes. Rewrites macOS `mpv.pc` for libarchive. Excluding docutils/HTML or yt-dlp changes formula scope and needs an explicit profile justification. |
