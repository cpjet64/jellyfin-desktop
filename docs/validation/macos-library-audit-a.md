# Tahoe formula audit A

Static inspection of stable formula metadata at homebrew-core `c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5`, assuming Tahoe x86_64. The source index contains **18**, not 22, alphabetically ordered nodes from autoconf through gettext inclusive; all 18 are covered below. Source URLs and SHA-256 values in [macos-media-source-index.md](macos-media-source-index.md) match the stable declarations for these nodes. This is not an evaluated Homebrew dependency graph, build, download, or proof that custom host tools match formula behavior. `B` denotes build, `T` test, and `N` no-linkage.

No formula in this group declares a stable patch or build resource. dav1d declares one test resource: `homebrew-00000000.ivf`, https://code.videolan.org/videolan/dav1d-test-data/raw/1.1.0/8-bit/data/00000000.ivf, SHA-256 `52b4351f9bc8a876c8f3c9afc403d9e90f319c1882bfe44667d41c8c6f5486f3`. This statement concerns formula declarations; vendored source contents and build-system downloads were not inspected.

| Formula | Tahoe stable direct dependencies and macOS-supplied inputs | Install and conditional details |
| --- | --- | --- |
| autoconf | m4; macOS perl | Forces `/usr/bin/perl`, replaces `libtoolize` with `glibtoolize`, configures prefix and lisp directory. |
| automake | autoconf | Forces `/usr/bin/perl`, modifies configure's data-directory expansion, sets sysconf/localstate/dataroot and global share paths plus standard configure args; writes aclocal dirlist. |
| bison | macOS m4 | Configures `--disable-dependency-tracking --enable-relocatable --prefix=/output M4=m4`, installs via DESTDIR, then copies staged output. |
| brotli | cmake B | CMake standard args and install rpath; builds and tests default build, installs it, then separately builds static with `BUILD_SHARED_LIBS=OFF`. |
| ca-certificates | No formula edge | Parses source PEM with Ruby OpenSSL and filters excluded public keys. Post-install merges trusted macOS keychain certificates with filtered Mozilla bundle; source PEM alone does not establish final bundle bytes or trust. |
| cairo | meson B, ninja B, pkgconf B/T; fontconfig, freetype, glib, libpng, libx11, libxcb, libxext, libxrender, lzo, pixman, gettext | Meson `--default-library=both`, explicitly enables fontconfig, freetype, png, glib, xcb, xlib, zlib and macOS quartz. X11 edges apply on macOS. |
| certifi | python-setuptools B, python@3.15 B/T, ca-certificates N | Uses Python pip with standard pip arguments. Formula Python 3.15 cannot be silently replaced by host Python 3.13 when claiming formula parity. |
| cffi | python@3.14 B/T, python@3.15 B/T, pycparser; macOS libffi | Revision 1; installs for each selected Python with pip `build_isolation: true`. Isolated build dependencies and offline availability require explicit proof; host Python alone is not formula parity. |
| cmake | macOS ncurses | Bootstraps then builds and installs with Homebrew standard CMake arguments; Linux OpenSSL dependency does not apply. Host CMake substitution removes this source build only by explicit custom-build choice. |
| dav1d | meson B, ninja B, nasm B on Intel | Meson setup/compile/install with standard Meson args. Test resource above is not a build resource. |
| deno | cmake, lld, llvm, ninja, pkgconf, rust B; little-cms2, sqlite; macOS python B and libffi; Xcode >=15 B | Uses LLVM clang, sets `GN_ARGS` with `use_lld=false` on macOS, system libffi/LCMS/sqlite and Python/Ninja paths. Cargo install uses `--no-default-features` with `deno_core/v8` and `v8/v8`. Source archive alone does not prove Cargo/V8 inputs are offline complete. |
| fast_float | cmake B | CMake configure and install using standard args. |
| ffmpeg | pkgconf B, nasm B; dav1d, lame, libvmaf, libvpx, openssl@4, opus, sdl2-compat, svt-av1, x264, x265, xz; macOS bzip2 and libxml2 | Revision 1. Enables shared, pthreads, GPL/version3, ffplay, named codecs/OpenSSL and macOS VideoToolbox/AudioToolbox. `alltools` also built. Classic linker flag is conditional on ld64 version 1015.7–1022.1; Intel does not enable ARM NEON. |
| flac | pkgconf B; libogg | Stable configure `--enable-static` plus standard args. Autoconf, automake, libtool and `autogen.sh` are HEAD-only. |
| fontconfig | gettext, meson, ninja, pkgconf B; freetype and gettext runtime; macOS gperf/python B and expat | Meson builds both library forms, disables docs/tests/cache build and additional font dirs, enables tools and supplies macOS font directories. Post-install invokes `fc-cache`; this is an external state effect requiring separate consideration. |
| freetype | pkgconf B; brotli, libpng; macOS bzip2 | Configures `--enable-freetype-config --with-brotli --without-harfbuzz`; edits pkg-config template and installed prefix references. Linux zlib-ng edge does not apply. |
| fribidi | help2man, pkgconf B | Configures `--disable-debug --disable-dependency-tracking --disable-silent-rules --enable-static` and prefix. |
| gettext | json-c, libunistring; macOS curl, libxml2, ncurses | Tahoe selects `am_cv_func_iconv_works=yes`; clang build >=1500 adds `-Wno-incompatible-function-pointer-types`. Includes glib/libcroco/gettext, uses emacs/lisp dir, disables Java/C#, Git, CVS and xz; install is serialized. |

## Findings for candidate documents

The component and FFmpeg lock proposals correctly label custom host tool substitution and unresolved standard arguments; they do not constitute the full mpv source closure. This audit also includes certifi, cffi and Deno because it used the broader source index; they are outside the helper-substituted library candidate graph and do not imply those builds are retained. The source index's first-source-only structure cannot by itself capture CA post-install transformation, formula environment substitutions, system SDK inputs or post-install side effects. No source-index hash discrepancy was found in this lane.

Configure defaults, exact Homebrew standard arguments, platform tool availability, dynamic linking, generated content and network behavior remain unverified. `deny_network_access!` exists on many but not all of these formulae; its presence is not proof that a custom build will work offline. Execute no build from this report until all transitive inputs, explicit feature choices and required acceptance evidence are resolved.
