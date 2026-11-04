# Tahoe Intel media formula audit B

Metadata audit against Homebrew core `c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5`, stable formulae, Tahoe x86_64. This covers the alphabetic `giflib` through `libsamplerate` segment of `macos-media-binary-helper-graph.md`: that segment contains **18 nodes**, rather than the 22 stated in the assignment. It does not claim that the complete graph is correct. No source archives were downloaded, built or verified. Versions below and the source URLs and SHA-256 values in `macos-library-sources.md` were compared to the pinned formula declarations, not to fetched archive bytes. HEAD, Linux, ARM and test-only inputs are identified separately from stable build requirements.

## Formula findings

| Node and source version | Stable Tahoe x86_64 declarations and noteworthy formula behavior |
| --- | --- |
| giflib 6.1.3 | No named formula or `uses_from_macos` dependencies, resource or patch declared. Make uses the selected prefix and `LIBUTILSO=` on macOS. |
| glib 2.90.1 | Build: bison, gettext, meson, ninja, pkgconf, python-setuptools, python@3.15. Runtime: pcre2 and, on macOS, gettext. Uses macOS flex, libffi and Python. Stages gobject-introspection 1.86.0; applies `Patches/glib/hardcoded-paths.diff` from the core tree. Its staged bootstrap and prefix-dependent settings are described below. |
| gobject-introspection 1.86.0, revision 6 | Build: bison, meson, ninja; dependencies cairo, glib, pkgconf, python@3.15; uses macOS flex and libffi. Four Python resources and an embedded `patch :DATA`; see below. The formula sets scanner cache disabled and modifies paths before Meson setup. |
| graphite2 1.3.15 | cmake build dependency; test-only `testfont` resource. Sets CMake install RPATH. |
| harfbuzz 15.0.0, revision 1 | Build: gobject-introspection, meson, ninja, pkgconf, python@3.15; runtime: cairo, freetype, glib, graphite2, icu4c@79, libpng. `pygobject3` and `homebrew-test-ttf` are test-only. Enables cairo, coretext, freetype, glib, gobject, graphite, ICU and introspection; disables tests. |
| help2man 1.49.3, revision 5 | gettext; macOS Perl; build resource `Locale::gettext` 1.07. Configures with `--enable-nls`. |
| icu4c@79 79.1 | No named formula dependencies or resources/patches declared. Configures `--disable-samples --disable-tests --enable-static --with-library-bits=64`. |
| jpeg-turbo 3.2.1 | cmake build; nasm build on Intel. Configures `-DWITH_JPEG8=1` and a linker RPATH. ARM-only floating-point flags do not apply to Intel. Formula invokes ctest before installation. |
| json-c 0.19 | cmake build; sets `-DBUILD_APPS=OFF`. No stable resources or patches declared. |
| lame 4.0 | pkgconf build, mpg123, macOS ncurses. Disables C23 configure detection and adds `-Wno-implicit-function-declaration`; configures `--enable-nasm`. No nasm dependency is declared by the formula: ensure the selected assembler is present and verify actual configuration rather than infer it from the graph. |
| libarchive 3.8.11 | libb2, lz4, xz, zstd; macOS bzip2 and expat. Configures `--without-lzo2 --without-nettle --without-xml2 --without-openssl --with-expat`; rewrites its pkg-config prefix to the Homebrew opt path. Linux zlib dependency does not apply. |
| libass 0.17.5, revision 1 | pkgconf build, freetype, fribidi, harfbuzz, libunibreak; nasm build on Intel. Configures `--disable-fontconfig` on macOS. Autoconf, automake and libtool are HEAD-only, and Linux fontconfig is not a macOS edge. |
| libb2 0.98.1 | No named formula dependencies. Applies core patch `Patches/libtool/configure-big_sur.diff`; configures `--enable-fat` on Intel, plus disabled dependency tracking and silent rules. |
| libbluray 1.5.0 | meson, ninja, pkgconf build; fontconfig, freetype, libudfread; macOS libxml2. Sets `-Dbdj_jar=disabled -Dfontconfig=enabled -Dfreetype=enabled -Dlibxml2=enabled`. |
| libogg 1.3.6 | cmake build; `oggfile` is test-only. Builds and installs shared, then builds static with CMake and installs its static library. |
| libplacebo 7.387.0 | Build: fast_float, meson, ninja, pkgconf, python@3.15, vulkan-headers. Runtime: little-cms2, shaderc, vulkan-loader. Three Python resources; see below. Enables lcms, shaderc and Vulkan and selects the Vulkan headers registry. |
| libpng 1.6.59 | No named stable Tahoe formula dependency or applicable resource/patch; the pngtest resource is Linux-only. Configures `--disable-silent-rules`, builds, runs `make test`, installs, and rewrites its pkg-config prefix to Homebrew opt. |
| libsamplerate 0.2.2 | cmake and pkgconf build; two remote patches below. Sets `-DLIBSAMPLERATE_EXAMPLES=OFF`; builds shared and static libraries separately. |

The graph's quoted edges for these 18 nodes agree with the inspected declarations at this level. The graph does **not** by itself represent system dependencies, untracked executable requirements such as LAME's enabled nasm, resources, patches, formula build steps, test execution or host-prefix rewrites. The source index does not identify source archive expansion, transitive subproject downloads, toolchain versions, SDK provenance or license approval.

## Bootstrap and resource evidence

GLib's formula breaks its introspection cycle by building and installing a staging GLib with `-Dintrospection=disabled`, then building its separately declared gobject-introspection resource with `-Dcairo=disabled -Ddoctool=disabled -Dtests=false`, then rebuilding GLib with `--default-library=both -Dintrospection=enabled`. Its final options include `-Dbsymbolic_functions=false -Ddtrace=disabled -Dtests=false`; it rewrites `gio-2.0.pc` and `glib-2.0.pc`, including gettext paths. A plain topological list cannot encode this sequence. The staged resource is https://download.gnome.org/sources/gobject-introspection/1.86/gobject-introspection-1.86.0.tar.xz, SHA-256 `920d1a3fcedeadc32acff95c2e203b319039dd4b4a08dd1a2dfd283d19c0b9ae`.

gobject-introspection's stable Python resources are mako 1.4.3 (`cd6537fe88d5fec315c55c2f8529bc4ce7a9a352ad7db3eeaa6a66e2dd4ec37a`), markdown 3.11.2 (`87bf5d90f2d2882e2c02fcda182c2800456b985bff38913867c49706481514b0`), markupsafe 3.0.4 (`2e9ad7dd851bf45fab9f75cbff4cb493fee9979e8d8c7c9c3ee119022518edd6`), and setuptools 84.2.1 (`3ff8b3b5492a0aa66960f4419365a86b84e013070d4313e4d4a6894f5d64d6bc`); exact URLs are in the pinned formula. The formula sets `-Dpython=<venv>/bin/python -Dextra_library_paths=<Homebrew prefix>/lib -Dtests=false`; it also modifies hard-coded data and library paths. Byte identities for the embedded and core patches are recorded in the resource and local-patch indexes; their prefix adaptations and relocation still require review.

help2man's resource is https://cpan.metacpan.org/authors/id/P/PV/PVANDRY/gettext-1.07.tar.gz, SHA-256 `909d47954697e7c04218f972915b787bd1244d75e3bd01620bc167d5bbc49c15`. libplacebo's resource URLs and hashes are recorded in `macos-library-mpv-lock.md`, which also records its external fast_float and Vulkan registry handling.

libsamplerate's two patches are https://github.com/libsndfile/libsamplerate/commit/e4a0ab46887029e0f65f145ba3987cc592f18200.patch?full_index=1, SHA-256 `0826fb59d733188645f3dc207c938580970700381b9bc87058b137723cb30bba`, and https://github.com/libsndfile/libsamplerate/commit/1abc639420b2df8b9ff2e0bdcc28cf6613c7c0d0.patch?full_index=1, SHA-256 `b6fb61c763a0f072ef2ebb3a311037d2429a5e294141f6d5e552ccd25efabdc0`.

## Open validation

The formulae rely on Homebrew's standard arguments, SDK/toolchain setup and system-library selection; their exact effects and the proposed isolated prefix must be verified on the selected Tahoe host. Test-only resources remain relevant if those upstream tests are required. `--enable-nasm` must not silently degrade; compiler compatibility, include and library resolution, both `.pc` rewrites, staging bootstrap, licenses, and actual flags all require evidence. This audit does not prove the 18 archives or patches were acquired, their transitive requirements are complete, or the compiled media features work.
