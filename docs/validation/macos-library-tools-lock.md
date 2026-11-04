# macOS library tools: candidate lock boundaries

This is a metadata-only assessment of the x86_64 Tahoe source candidate, not
a complete lock or build authorization. The reference is Homebrew core
[`c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5`](https://github.com/Homebrew/homebrew-core/tree/c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5/Formula). Source versions and hashes below identify
formula inputs; selected bytes, compiler, SDK, output and full transitive
closure have not been verified by this document.

## External build tools

| Input | Pinned formula source SHA-256 | Role and remaining qualification |
| --- | --- | --- |
| Meson 1.12.1 | `ab0a6ca09f8ef70c564c8241fb5a23957886a0b53fb58412b5e07eaf07dba743` | Formula depends on Ninja and Python 3.15 and uses pip build isolation. Its architecture-independent bottle does not remove the Python dependency. A host-provided Meson must be version- and behavior-qualified, including its Python and build backend. |
| NASM 3.02 | `87336eba53b4acfe917424ab5d500d2b0054d9f5148d35c2273ccf2cfb712f0d` | Intel assembler needed by selected codec builds and libass. Formula's stable source must be distinguished from HEAD-only autoconf, automake, asciidoc and xmlto requirements. An external executable needs architecture, version and output qualification. |
| Python 3.15.2 | `f74e1db0eabd651cd01e4e2a00201bd41866abe74869ab716f9bf33cbbaa0943` | Formula depends on mpdecimal, OpenSSL 4, SQLite, xz and zstd, plus platform libraries and CA certificates. It includes flit-core 4.1.0 and pip 26.2.2 resources and a Homebrew sysconfig patch. Formula build configuration includes IPv6, loadable SQLite extensions and optimizations, with a macOS framework build. |

The separately reported host Python 3.13 and probed tools are potential build
tools, not substitutes for formula Python 3.15 without compatibility testing.
The pinned [Meson 1.12.1 launcher](https://github.com/mesonbuild/meson/blob/1.12.1/meson.py)
rejects Python versions below 3.10, so observed Python 3.13.15 satisfies that
specific version gate. This does not verify the interpreter, Meson imports,
build backend, isolation behavior or any project's generators.
The earlier bounded prerequisite observation recorded Ninja 1.13.2 and
pkg-config 3.0.6, and did not find Meson on its selected PATH; these are
historical observations, not a present host inventory. Verify exact paths,
versions, architecture and dependencies again before use. VapourSynth 82
declares Python 3.15 as a runtime dependency, and gobject-introspection ships a
CPython 3.15-specific scanner; replacing build Python does not eliminate those
runtime or ABI requirements. No Python 3.13 compatibility claim follows.

The existing candidate graph helper stops dependency expansion at Meson,
CMake, Ninja and pkgconf; this is an external-tool boundary, not proof of a
closed or reproducible graph. It also uses text-based conditional filtering,
so its candidate edges require formula-by-formula review. Explicitly record
which executable and Python environment satisfy each boundary, and account for
Meson's isolated build backend and any network access. Do not silently use
global package installs or unpinned dependencies.

For Tahoe, the selected formula declarations also use macOS-supplied Perl,
m4, bzip2, libxml2, gperf, Python, expat, curl, ncurses, flex, libffi,
libedit and vim/xxd where applicable. Python's expat system fallback applies
since Sequoia, so Tahoe does not select its older-OS expat formula edge.
VapourSynth's LLVM edge applies only on Ventura or older and is absent on
Tahoe. These are declared system boundaries, not proof that the selected
host tools and SDK libraries are suitable; qualify the exact paths and
versions, and fail rather than silently selecting another installation.

## Generator and font stack

| Formula | Source hash and selected formula behavior |
| --- | --- |
| GLib 2.90.1 | `93c941aa17d5eb1d53fe838365f29a8b4e539c222a256d974ec8f30fc413e396`; Meson, Ninja, pkgconf, bison, gettext, setuptools and Python 3.15 build dependencies; pcre2 runtime. Uses a staged GLib build with introspection disabled, then a gobject-introspection bootstrap with cairo and doctool disabled, then a final `--default-library=both` and introspection-enabled build. Common flags disable dtrace and tests, set `bsymbolic_functions=false`, and specify runtime and GIO module paths. |
| GObject Introspection 1.86.0 revision 6 | `920d1a3fcedeadc32acff95c2e203b319039dd4b4a08dd1a2dfd283d19c0b9ae`; depends on cairo, GLib, pkgconf and Python 3.15; build needs bison, Meson, Ninja, macOS flex and libffi. Its formula uses a Python virtual environment and sets its Python and extra library path explicitly, with tests disabled. |
| Cairo 1.18.6 | `1c767308174337a74694da0f3ec069c271452163a1ef4540964c50c301f157d4`; depends on fontconfig, FreeType, GLib, PNG, X11/XCB/Xext/Xrender, lzo and pixman. Formula enables both static and shared libraries, fontconfig, FreeType, PNG, GLib, XCB, Xlib and zlib, and enables Quartz on macOS. |
| HarfBuzz 15.0.0 revision 1 | `968536e9abd782f07f7954044c0c4020199fcdd39b41d221ec42d2a3be4a44df`; build depends on gobject-introspection, Meson, Ninja, pkgconf and Python 3.15; runtime includes Cairo, FreeType, GLib, graphite2, ICU 79 and PNG. Formula enables both library types, Cairo, CoreText, FreeType, GLib, GObject, Graphite, ICU and introspection, with tests disabled. PyGObject is a formula-test dependency, not thereby a production dependency. |
| FreeType 2.15.0 | `93cd47eb719c325107f2d3cf087d06299e944c3b48a40b55537302304dea1dd5`; depends on Brotli and PNG and uses macOS bzip2; configure enables freetype-config and explicitly disables HarfBuzz. |
| FriBidi 1.0.18 | `1de5e8073d035388b71d0b2d452b1da1f8caeea8a7702997b401fe1b6250727f`; configure enables static output and disables debug, dependency tracking and silent rules. |

GLib's bootstrap embeds gobject-introspection 1.86.0 as a source resource.
The separate gobject-introspection formula includes Mako 1.4.3, Markdown
3.11.2, MarkupSafe 3.0.4 and setuptools 84.2.1 resources. Their hashes and
URLs, as well as HarfBuzz's test-only font, are in the pinned formulae and
the [resource index](macos-media-resource-index.md). A complete lock must distinguish bootstrap, build, runtime
and optional test inputs, record all applicable patches and resources, and
resolve the GLib/Cairo/introspection cycle in the documented staged order.
Disabling introspection, Cairo, X11 or other features to reduce the graph is
a profile change requiring explicit comparison and Desktop behavior evidence;
it is not established by the helper's exclusions.

Additional formula install stages and relocation-sensitive details:

- Fontconfig 2.18.3 source SHA-256 is
  `9ae01e1d53acdef56010c5451cd34aa41d325b2faccd8606448d8fa01b2496b3`.
  It depends on FreeType and builds with gettext, Meson, Ninja, pkgconf,
  macOS gperf and Python; macOS expat is used. Its formula enables both library
  types and tools, disables docs, tests, build-time cache and additional font
  directories, and selects system, local, user and newest matching MobileAsset
  font directories. The post-install `fc-cache --force --really-force
  --verbose` is a host-mutating operation, not authorized by this assessment.
- GLib adds its staging pkg-config directory and binaries to the environment
  before bootstrapping introspection. It rewrites GIO and GLib pkg-config
  paths, including gettext paths on macOS, and declares a post-install GIO
  module directory. A private prefix needs equivalent path and relocation
  verification. FreeType explicitly uses `--with-brotli --without-harfbuzz`
  and rewrites its installed config and pkg-config prefixes.
- The separate introspection formula installs its resources in a virtualenv,
  disables the scanner cache, rewrites scanner paths and shebangs, and uses an
  inline patch to implement `extra_library_paths`. Unpatched source with that
  option is not formula parity. Capture the patch and verify private paths.
- Python's macOS configure additionally uses `--without-ensurepip`,
  `--with-system-expat`, `--with-system-libmpdec`, `--with-readline=editline`,
  `--with-lto`, `--with-dtrace`, `--with-dbmliborder=ndbm`, a selected OpenSSL
  prefix and a deployment target tied to the build host. It selects `x64` for
  Intel libmpdec and conditionally enables the tail-call interpreter based on
  Apple Clang. It then creates a local flit-core/pip wheel environment, replaces
  the bundled pip wheel, and runs ensurepip and a no-index pip install.
  Framework paths, sysconfig, ctypes fallback paths, certificates and generated
  shebangs need review for a relocatable private prefix. These stages remain
  open; metadata does not prove that a different Python build is equivalent.

Formula flags are intended settings, not proof of successful autodetection or
linkage. The host probe selected SDK 26.5 and Apple clang 21. Remaining inputs include deployment target, all
transitive source and patch hashes, applicable formula conditions, compiler
and tool identities, generated resources, library install names and runtime
closure, license review, space estimates, and source build ordering. Verify
ASS shaping, CoreText font selection, embedded playback and actual output
features on the target. No downloads, builds or host changes were performed
for this assessment.
