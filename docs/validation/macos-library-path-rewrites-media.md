# Media formula path adaptation for a job-owned prefix

This is a metadata-only audit of the stable formula install methods in Homebrew core commit `c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5`, for Intel macOS Tahoe. The intended build and installation prefix is job-owned. The formula transformations below describe the reference exactly; proposed adaptations and required tests are not yet validated instructions. Standard Homebrew arguments and build-environment behavior are not expanded here. No source archives were downloaded, no builds were run, and no bundle relocation has been proven.

## mpv

The formula supplies `--sysconfdir=#{etc}` and Homebrew `std_meson_args`. After installation on macOS, it rewrites `lib/pkgconfig/mpv.pc`: the regex `/^Requires\.private:(.*)\blibarchive\b(.*?)(,.*)?$/` is replaced with `Requires.private:\\1#{formula_opt_prefix("libarchive")}/lib/pkgconfig/libarchive.pc\\3`. This assumes Homebrew's libarchive opt layout. The replacement also discards the regex's second captured group; preserve and inspect actual generated content rather than assume its shape.

For a job-owned prefix, select a private configuration location and provide a pkg-config resolution strategy that does not point into Homebrew. Whether a relative `Requires.private`, a prefix-local absolute `.pc` path, or another equivalent is correct is unresolved. Verify `pkgconf --print-errors` and the complete static and shared link outputs using the isolated prefix, then repeat after bundle relocation. A successful `mpv.pc` parse does not establish runtime dylib or configuration discovery.

## libplacebo

The formula stages `glad2` and `jinja2` resources under `3rdparty/glad` and `3rdparty/jinja` (removing trailing digits from those resource names); it stages markupsafe under `3rdparty/markupsafe`. It replaces the literal `../3rdparty/fast_float/include` in `src/meson.build` with `formula_opt_include("fast_float")`. Meson receives `-Dvulkan-registry=#{Formula["vulkan-headers"].share}/vulkan/registry/vk.xml`, as well as enabled lcms, shaderc and Vulkan and `std_meson_args`.

Resolve fast_float and `vk.xml` from the job-owned dependency prefix, verify those files and the selected pinned identities, and check the configured build for fallback to bundled or host paths. Inspect generated pkg-config metadata, linked dylibs, install names and RPATHs. Runtime Vulkan selection and color/scaling behavior remain separate tests.

## shaderc

The formula stages each pinned resource under `third_party/<resource-name>`. It replaces the literal `${SHADERC_SKIP_INSTALL}` with `ON` in `third_party/CMakeLists.txt`, and removes the exact line fragment `set(GLSLANG_ENABLE_INSTALL $<NOT:${SKIP_GLSLANG_INSTALL}>)`. It configures CMake with `SHADERC_SKIP_TESTS`, `SKIP_GLSLANG_INSTALL`, `SKIP_SPIRV_TOOLS_INSTALL`, and `SKIP_GOOGLETEST_INSTALL` set to `ON`, plus `std_cmake_args`.

These are build-tree edits, not proven relocation fixes. A private build needs equivalent resource staging, an explicit installation prefix, and checks that substitutions each matched once and third-party fetches or unintended installs do not occur. Verify resulting headers, library discovery and dylib linkage after relocation; no source-derived install-name or runtime guarantee is established here.

## MoltenVK

The formula stages resources under `External/<resource-name>`, moves `External/SPIRV-Headers` to `External/spirv-tools/external/spirv-headers`, and uses CMake and Xcode project builds. It installs `libMoltenVK.dylib`, static products and an xcframework, plus shader converter products. For its ICD manifest, it replaces `./libMoltenVK.dylib` in `MoltenVK/icd/MoltenVK_icd.json` with the path from `prefix/etc/vulkan/icd.d` to the installed `lib/libMoltenVK.dylib`, then installs the manifest in `prefix/etc/vulkan/icd.d`.

Compute the manifest's library path from the actual manifest location and chosen library destination; the reference relative path is valid only for its reference layout. Validate the JSON after installation, verify that its resolved target is the intended x86_64 library, and test loader discovery with only the approved environment after moving the bundle. Confirm install names, RPATHs and transitive dylibs separately. The required `xcodebuild` projects, compiler/SDK compatibility, Metal runtime and final packaging remain unproven.

## Vulkan loader

The formula passes these paths to CMake:

- `VULKAN_HEADERS_INSTALL_DIR=#{formula_opt_prefix("vulkan-headers")}`;
- `CMAKE_INSTALL_INCLUDEDIR=#{formula_opt_include("vulkan-headers")}`;
- `CMAKE_INSTALL_SYSCONFDIR=#{etc}`;
- `FALLBACK_CONFIG_DIRS=#{etc}/xdg:/etc/xdg`;
- `FALLBACK_DATA_DIRS=#{HOMEBREW_PREFIX}/share:/usr/local/share:/usr/share`.

It also supplies Homebrew `std_cmake_args`. In a job-owned build, select the verified header prefix and explicit include and configuration destinations, and decide and test permitted fallback paths. The reference fallback data paths expressly include host locations; they do not prove isolated or relocatable discovery. Confirm that the loader locates only the intended ICD when tested in the approved environment, and inspect all dylib dependencies. Whether any system fallbacks are acceptable is an open packaging decision.

## VapourSynth

The formula enables runtime CPU detection. Its extra llvm C++ linker path applies on macOS Ventura or older, not Tahoe. It stages the glslang resource at `subprojects/glslang`, copies `subprojects/packagefiles/glslang/*` into that directory, then creates a virtual environment with its resources. It writes a `homebrew-vapoursynth.pth` file in `prefix/<Python site-packages>` containing the virtual environment's site-packages path. It creates a plugin symlink inside the virtual environment's `vapoursynth` directory pointing to `HOMEBREW_PREFIX/<Python site-packages>/vapoursynth/plugins`, and a `lib/pkgconfig/vapoursynth.pc` symlink pointing to the virtual environment's `vapoursynth/pkgconfig/vapoursynth.pc`.

Both the `.pth` content and symlink targets must be evaluated for the job prefix and final layout. A private equivalent must retain the expected Python package and plugin discovery without dangling links or user/host-prefix leakage. Check the staged glslang source, the actual `.pth` and symlink targets, pkg-config output, Python import, plugin loading and resulting dylibs before and after relocation. This is not evidence that Python or plugins are bundled or that VapourSynth functions in the application.

## Cross-cutting unresolved work

The formulae reference Homebrew's `etc`, `prefix`, opt paths, standard build arguments and environment; a custom build cannot assume their exact expansion or transitive effects. Define a job-owned build tree, install tree, allowed SDK/system inputs and final bundle layout before choosing substitutions. Record exact commands and generated files, assert replacement counts and resource identities, prohibit implicit fetches and accidental host-prefix inputs, and test x86_64 slices, deployment targets, install names, RPATHs, configuration, ICD, Python and plugin discovery after relocation. No lock completeness, feature parity, playback, or acceptance claim is made by this document.
