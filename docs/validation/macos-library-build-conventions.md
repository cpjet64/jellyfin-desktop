# Proposed Tahoe library build conventions

These settings define the common part of the proposed custom job-owned build;
the component-specific options and staged dependency order remain in the
formula audits and library proposal. They are not commands that have run.
The library's deployment target is proposed as macOS **12.0**, preserving the
repository's stated Intel minimum. SDK 26.5 and clang 21 on Tahoe do not prove
that all selected sources or APIs work on 12.0: compile/link availability,
resulting Mach-O minimum OS and actual 12.0 compatibility must pass before a
12+ claim. Do not raise that minimum silently if the build fails.

The following Homebrew/brew files are a separately selected metadata reference
at commit `bf2b7b9ef8940ec3c94fa3ec1da95fe484963c10`; this is not evidence
that the core reference was historically built with that exact brew commit:

- [Formula helpers](https://raw.githubusercontent.com/Homebrew/brew/bf2b7b9ef8940ec3c94fa3ec1da95fe484963c10/Library/Homebrew/formula.rb), SHA-256 `c6c1b0139de3644e3e964747b6a7ae3b686f63abc06f113c21d282fe9d360044`.
- [macOS formula helper](https://raw.githubusercontent.com/Homebrew/brew/bf2b7b9ef8940ec3c94fa3ec1da95fe484963c10/Library/Homebrew/extend/os/mac/formula.rb), SHA-256 `a85eab89b8f5522ef45561d2550dbcc0460e0ec0bd67c89a34753be81834fe31`.
- [FetchContent trap](https://raw.githubusercontent.com/Homebrew/brew/bf2b7b9ef8940ec3c94fa3ec1da95fe484963c10/Library/Homebrew/cmake/trap_fetchcontent_provider.cmake), SHA-256 `b81d3934a6c2bafe509a1961c429e2aa37f988028d272f5a6039d2c8ff88b8ea`.

The selected reference expands its standard configure arguments to
`--disable-debug --disable-dependency-tracking --prefix=<prefix>
--libdir=<prefix>/lib`. Its Meson defaults are `--prefix=<prefix>
--libdir=lib --buildtype=release --wrap-mode=nofallback`. Its CMake defaults
set install prefix, `lib` install directory, Release mode, framework search
LAST, verbose makefiles, `BUILD_TESTING=OFF`, `CCACHE_FOUND=OFF`, `-Wno-author`
and the FetchContent trap; the macOS helper also sets `CMAKE_OSX_SYSROOT`.
Component formula calls may override these values, including staging prefixes
and SDL2 compatibility's distinct configure/install prefix. They must be
expanded per component rather than applied blindly.

For this custom proposal, set `PREFIX` to the absolute job-owned prefix,
`SDKROOT` to the verified SDK 26.5, `MACOSX_DEPLOYMENT_TARGET=12.0`, and
architecture `x86_64`. Configure receives the reference options above with
`PREFIX`; Meson receives those reference options except the stricter
`--wrap-mode=nodownload`; CMake receives the reference options with
`CMAKE_OSX_SYSROOT`, `CMAKE_OSX_ARCHITECTURES=x86_64`,
`CMAKE_OSX_DEPLOYMENT_TARGET=12.0` and the pinned FetchContent trap staged as
a verified small input. Use compiler/linker flags equivalent to `-arch x86_64`
and `-mmacosx-version-min=12.0` when a component does not consume CMake/Meson
target settings. `nodownload` permits local fallback; permit only explicitly
listed, hash-verified staged subprojects such as VapourSynth's glslang, and
reject unlisted fallback use. Record actual values and fail on conflicts or an attempted
network fetch. Use at most two compile jobs and one component build at a time.

Set pkg-config's `PKG_CONFIG_LIBDIR` to the job-owned pkgconfig directories,
clear inherited `PKG_CONFIG_PATH`, and set CMake prefix search to the job-owned
prefix and separately approved Qt prefix only when appropriate. Pin SDK system
libraries explicitly and verify actual resolved paths; neither this convention
nor a successful configure proves no host leakage. Do not add `/usr/local` or
Homebrew global prefixes as fallback search paths. The GLib bootstrap is an
explicit exception: its verified job-owned staging pkgconfig and executable
directories precede the final prefix only for the recorded bootstrap stages.
Review each formula's
environment and in-tree path rewrite before use; ensure Fontconfig cache and
other post-install work remains job-owned.

The reference pip helper uses `--no-deps --no-binary=:all: --ignore-installed`
and `--no-compile`, and normally `--no-build-isolation`; some formulae request
isolation. This proposal instead requires an already staged, hash-verified
offline build backend and dependencies, `--no-index --no-deps` and no implicit
build isolation downloads. The exact Python build backend and Meson/Python
integration are still unresolved, so the conventions are not a complete
offline build lock. Required feature checks, dylib closure, resource staging,
trust data and storage proof remain separate gates.
