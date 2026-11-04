# Intel macOS mpv library source lock: partial

Status: **incomplete discovery, not a buildable lock**. Target assumptions are Intel macOS Tahoe, SDK 26.5 and Apple clang 21. The SDK and compiler are host-probe selections, not proof that this media stack builds or runs with them. Reference metadata is Homebrew core commit `c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5`. Formula URLs below use `https://raw.githubusercontent.com/Homebrew/homebrew-core/c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5/Formula/` as their base. This document covers the library path; FFmpeg and helper selection are separate work. No downloads, builds, or byte verification were performed for this document.

## Formula source identities

| Formula | Stable source | SHA-256 | Notes |
| --- | --- | --- | --- |
| `m/mpv.rb` | https://github.com/mpv-player/mpv/archive/refs/tags/v0.41.0.tar.gz | `ee21092a5ee427353392360929dc64645c54479aefdb5babc5cfbb5fad626209` | 0.41.0, formula revision 12 |
| `lib/libass.rb` | https://github.com/libass/libass/releases/download/0.17.5/libass-0.17.5.tar.xz | `2dca25c0e0c837ddf00b52011b3f82cac1e4ddd3ad018227806b0c2288864acc` | 0.17.5, formula revision 1 |
| `lib/libplacebo.rb` | https://code.videolan.org/videolan/libplacebo/-/archive/v7.387.0/libplacebo-v7.387.0.tar.bz2 | `14ceeeb14d1c08fcdb1a782a06129da11f842c6fddbf3a43c1c4061406ad03d2` | 7.387.0 |
| `m/molten-vk.rb` | https://github.com/KhronosGroup/MoltenVK/archive/refs/tags/v1.4.2.tar.gz | `6864db532f1dbbdb621a8d0ec13f24edae318fd9269dd3dd0cdff791334bb1cb` | 1.4.2 |
| `s/shaderc.rb` | https://github.com/google/shaderc/archive/refs/tags/v2026.4.tar.gz | `f06ce5bcca94e5df7f34e115743597d0ad2e13c5fe9213c67dc8c76031241947` | 2026.4 |
| `v/vapoursynth.rb` | https://files.pythonhosted.org/packages/25/2e/1a5cb192ce26077c5afe9056a11b3a2d936c4e413b4fec5eee74d45bdb01/vapoursynth-82.tar.gz | `1128316620890ba8e5b69c924a665f98001b2ba4230dadead0f296446ea71843` | 82 |
| `v/vulkan-headers.rb` | https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz | `4a078be12bef21cfebc09d878b77a63cff9d68f899254a0b00d0e37ef73e7f7e` | Vulkan SDK 1.4.363.0 |
| `v/vulkan-loader.rb` | https://github.com/KhronosGroup/Vulkan-Loader/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz | `941eff558fc74f248745fb7df367640cf89a8457f216dae498a74ae14b6ba6f7` | Vulkan SDK 1.4.363.0 |

## Patches and resources

The stable mpv formula applies these backports:

| Patch | SHA-256 |
| --- | --- |
| https://github.com/mpv-player/mpv/commit/75b2ccfeb1ce4ed5a40ac9860fa74f3d1265e13f.patch?full_index=1 | `3906b98b02071a0d5747a400406494ca69cef7afd8d3eee4a99fdbe40dc90c1f` |
| https://github.com/mpv-player/mpv/commit/c5d391adba7bd024954d0df1e0405f5749f4d4ca.patch?full_index=1 | `769b218df220738cc1cf9f81cf696c16518c5dfe56a5ef028e33b22536e0e924` |

libplacebo stages these Python resources:

| Resource | Source | SHA-256 |
| --- | --- | --- |
| glad2 2.0.8 | https://files.pythonhosted.org/packages/6e/5a/d62b24fe1c7c2f34e15c2aa4418a5327a8550fdc272999a59e0dddebc3ee/glad2-2.0.8.tar.gz | `b84079b9fa404f37171b961bdd1d8da21370e6c818defb8481c5b3fe3d6436da` |
| jinja2 3.1.6 | https://files.pythonhosted.org/packages/df/bf/f7da0350254c0ed7c72f3e33cef02e048281fec7ecec5f032d4aac52226b/jinja2-3.1.6.tar.gz | `0137fb05990d35f1275a587e9aee6d56da821fc83491a0fb838183be43f66d6d` |
| markupsafe 3.0.4 | https://files.pythonhosted.org/packages/38/9b/e422a865e1d5d57d0e509b4e0bf1c1a70a7f6382c29a5aa428df994c8bc8/markupsafe-3.0.4.tar.gz | `2e9ad7dd851bf45fab9f75cbff4cb493fee9979e8d8c7c9c3ee119022518edd6` |

Shaderc's stable resources are Git revisions: `https://github.com/KhronosGroup/glslang.git` at `e1b562a8bed273a02f30b59b66a5d499793cede5`, `https://github.com/KhronosGroup/SPIRV-Headers.git` at `04fd3caa1e8267e4d95c806cad901181728e1006`, and `https://github.com/KhronosGroup/SPIRV-Tools.git` at `ef96ed763b43b59b33b31b362f09a02b729fa1c9`.

MoltenVK's stable Git resources are:

| Repository under `https://github.com/` | Revision |
| --- | --- |
| KhronosGroup/SPIRV-Cross.git | `6c09849fe88c48eaed08413aa022aaa136a3a057` |
| KhronosGroup/SPIRV-Headers.git | `29981f65241605e08b0ede4cfeb999fe3b723c6a` |
| KhronosGroup/SPIRV-Tools.git | `0d6fd73ca73830ccab5fa1f00ed5ed40124e2c55` |
| KhronosGroup/Vulkan-Headers.git | `e3b1eec08173d6b825cd3ac88c885a63b621504a` |
| KhronosGroup/Vulkan-Tools.git | `8c66b352925cb771f793a4d3220b1321ae0febf1` |
| USCiLab/cereal.git | `a56bad8bbb770ee266e930c95d37fff2a5be7fea` |

Vapoursynth's glslang resource is https://github.com/KhronosGroup/glslang/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz, SHA-256 `cda24ec765e6d2845d106b7caa593f34127195a88524cf87821ff0088fb6edec`. Git revisions identify repository objects; they are not hashes of generated archives.

## Dependency edges and reference flags

- libass needs pkgconf, freetype, fribidi, harfbuzz, libunibreak and Intel nasm. It configures `--disable-fontconfig` on macOS. Its autoconf, automake and libtool declarations are HEAD-only.
- libplacebo needs fast_float, meson, ninja, pkgconf, python@3.15 and vulkan-headers for build, plus little-cms2, shaderc and vulkan-loader. Its formula enables `-Dlcms=enabled`, `-Dshaderc=enabled` and `-Dvulkan=enabled`.
- shaderc needs cmake and macOS Python for build. Its formula sets `-DSHADERC_SKIP_TESTS=ON`, `-DSKIP_GLSLANG_INSTALL=ON`, `-DSKIP_SPIRV_TOOLS_INSTALL=ON`, and `-DSKIP_GOOGLETEST_INSTALL=ON`.
- The formula declares cmake, macOS Python and Xcode 11.7 or newer, but the pinned MoltenVK 1.4.2 upstream README requires **Xcode 15.0.1 or newer**. SDK 26.5, clang 21 and full-Xcode compatibility remain unproven. [Upstream requirement](https://github.com/KhronosGroup/MoltenVK/blob/db66022459ffb663aa2b50f6b018bc2e124f5edf/README.md)
- Vapoursynth needs ninja, pkgconf, vulkan-headers, python@3.15 and zimg. The formula's llvm dependency applies to Ventura and older, not Tahoe.
- mpv's reference formula needs FFmpeg, jpeg-turbo, libarchive, libass, libbluray, libplacebo, little-cms2, luajit, mujs, rubberband, uchardet, vapoursynth, vulkan-loader, yt-dlp, zimg and, on macOS, molten-vk. It uses docutils, meson, ninja and pkgconf for build. Its explicit Meson options are `-Dbuild-date=false`, `-Dhtml-build=enabled`, `-Djavascript=enabled`, `-Dlibmpv=true`, `-Dlua=luajit`, `-Dlibarchive=enabled`, `-Duchardet=enabled`, and `-Dvulkan=enabled`, plus its sysconfdir and standard Homebrew arguments.

### Build-system details to preserve or deliberately adapt

- MoltenVK stages all six resources under `External`, moves SPIRV-Headers into the SPIRV-Tools external tree, and first builds SPIRV-Tools with CMake. It then uses `xcodebuild` for the `ExternalDependencies-macOS` scheme and the `MoltenVK Package (macOS only)` scheme, with the active CPU architecture and `ONLY_ACTIVE_ARCH=YES`. The packaging invocation sets `MVK_CONFIG_LOG_LEVEL=MVK_CONFIG_LOG_LEVEL_NONE`. The extra xcframework creation branch is for clang build 1500 or newer on macOS older than Sonoma; it does not apply on Tahoe. Full Xcode and a usable `xcodebuild` are therefore required by this reference build, not established by an SDK and compiler link probe alone. Whether the selected Xcode, SDK and compiler can actually build this revision remains unverified.
- MoltenVK installs a dylib, static library and xcframework, plus shader converter artifacts and an ICD JSON. Its formula rewrites the JSON library path relative to the Homebrew prefix. Bundle ICD discovery and a relative path appropriate to the final layout require separate proof; Metal availability and actual Vulkan playback are also unproven.
- libplacebo stages glad2 and jinja2 under `3rdparty/glad` and `3rdparty/jinja`, respectively, and markupsafe under `3rdparty/markupsafe`. It replaces the bundled fast_float include path with the selected external fast_float include path, and supplies `-Dvulkan-registry=<vulkan-headers share>/vulkan/registry/vk.xml`. A custom build must provide the intended pinned header registry and prevent unintended host-prefix resolution.
- shaderc stages the three named resources under `third_party`, changes `third_party/CMakeLists.txt` to set `${SHADERC_SKIP_INSTALL}` to `ON`, and removes its `GLSLANG_ENABLE_INSTALL` generator-expression assignment. These in-tree edits are additional to the four CMake flags above and must be reproduced or replaced with a validated equivalent.
- vulkan-loader uses cmake, pkgconf and vulkan-headers. The reference sets `VULKAN_HEADERS_INSTALL_DIR`, `CMAKE_INSTALL_INCLUDEDIR`, `CMAKE_INSTALL_SYSCONFDIR`, `FALLBACK_CONFIG_DIRS` and `FALLBACK_DATA_DIRS` to Homebrew-specific locations. The fallback paths include system and Homebrew prefixes, so an isolated build must set and test appropriate locations rather than assume the reference values are relocatable. vulkan-headers uses CMake to install its headers and registry.

The reference formula rewrites `mpv.pc` to refer to the Homebrew libarchive opt path. A custom prefix and relocatable bundle need their own validated equivalent. Documentation omission is approved as a build-only difference, with HTML, manpage and PDF disabled and docutils omitted only if no other selected consumer needs it. Omitting yt-dlp would change the reference profile; Desktop's requested `ytdl=false` is not proof of its effective runtime value.

## Partial build order and remaining proof

Provision and verify the compiler, SDK, build tools and all transitive sources first. Build each dependency after its own prerequisites. In particular, build the shaping and font prerequisites before libass; Vulkan headers and loader, little-cms2 and shaderc before libplacebo; and MoltenVK and its pinned resources before any consumer that needs it. Build FFmpeg and the remaining reference dependencies before configuring mpv; then build libmpv and validate it through the Desktop application. This is a partial ordering, not a complete executable graph.

The remaining direct and transitive formula sources, exact resources, patches, flags, system dependencies, deployment target, library linkage and a full topological order are not locked here. The static 79-formula candidate discovery in `macos-mpv-components.md` is not an authoritative Homebrew evaluation. Verify every fetched byte and patch, disallow implicit network fetches and host-prefix fallback, and validate x86_64 architectures, install names, RPATHs, runtime capabilities and playback. No native build, ABI, graphics, packaging, offline reproducibility, license, or user acceptance claim follows from this metadata.
