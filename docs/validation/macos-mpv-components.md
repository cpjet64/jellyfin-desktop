# macOS mpv component discovery

This is source metadata discovery, not a complete source lock or build recipe.
The reference is Homebrew core commit `c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5`, stable formulae, macOS Tahoe on Intel. Formula links below are relative to
`https://raw.githubusercontent.com/Homebrew/homebrew-core/c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5/Formula/`.
No source archives, patches, builds, or runtime behavior were verified here.

The proposed Desktop profile retains yt-dlp because users can override the application's requested `ytdl=false` setting; its return value is unchecked. Documentation omission is approved as a build-only difference: explicitly disable HTML, manpage and PDF generation, and exclude docutils only if no other retained component consumes it. FFmpeg is tracked separately. All reference runtime dependencies remain candidates.

## mpv

`m/mpv.rb` selects mpv 0.41.0, revision 12:

- Source: https://github.com/mpv-player/mpv/archive/refs/tags/v0.41.0.tar.gz
- SHA-256: `ee21092a5ee427353392360929dc64645c54479aefdb5babc5cfbb5fad626209`
- Backport: https://github.com/mpv-player/mpv/commit/75b2ccfeb1ce4ed5a40ac9860fa74f3d1265e13f.patch?full_index=1; SHA-256 `3906b98b02071a0d5747a400406494ca69cef7afd8d3eee4a99fdbe40dc90c1f`.
- Backport: https://github.com/mpv-player/mpv/commit/c5d391adba7bd024954d0df1e0405f5749f4d4ca.patch?full_index=1; SHA-256 `769b218df220738cc1cf9f81cf696c16518c5dfe56a5ef028e33b22536e0e924`.

Reference build tools are meson, ninja, pkgconf, and docutils. Its Meson options enable HTML documentation, JavaScript, libmpv, LuaJIT, libarchive, uchardet, and Vulkan, and disable build dates. The approved documentation difference disables all three documentation outputs and conditionally omits docutils; other flags and feature behavior require validation. The formula has a macOS-specific `mpv.pc` adjustment for libarchive. It is not established that this adjustment is suitable for a relocated bundle.

Reference runtime dependencies, excluding the separately tracked FFmpeg: jpeg-turbo, libarchive, libass, libbluray, libplacebo, little-cms2, luajit, mujs, rubberband, uchardet, vapoursynth, vulkan-loader, yt-dlp, zimg, and macOS-only molten-vk. These must be available before configuring mpv; their own build and runtime prerequisites precede them. A complete topological order has not been proven.

## Important component edges and options

| Formula | Stable Tahoe Intel dependencies and notable options |
| --- | --- |
| `lib/libass.rb` | pkgconf (build), freetype, fribidi, harfbuzz, libunibreak; nasm (Intel build). macOS disables fontconfig. Autoconf, automake and libtool declared in its HEAD block are not stable dependencies. |
| `lib/libplacebo.rb` | fast_float, meson, ninja, pkgconf, python@3.15, vulkan-headers (build); little-cms2, shaderc, vulkan-loader. Enables lcms, shaderc and Vulkan. Its build resources are listed below. |
| `m/molten-vk.rb` | The formula declares cmake and Xcode 11.7 or newer; pinned MoltenVK 1.4.2 upstream requires Xcode 15.0.1 or newer. macOS only. Six stable Git resources are listed below. |
| `v/vapoursynth.rb` | ninja, pkgconf, vulkan-headers (build), python@3.15 and zimg. llvm is selected by the formula for Ventura or older, not Tahoe. Its glslang resource is listed below. |
| `s/shaderc.rb` | cmake and macOS Python for build; three stable Git resources below. Disables shaderc tests and installation of its embedded glslang, SPIRV-Tools and googletest. |
| `lib/libbluray.rb` | meson, ninja, pkgconf, fontconfig, freetype, libudfread and macOS libxml2. Enables fontconfig, freetype and libxml2; disables BD-J JAR. |
| `j/jpeg-turbo.rb` | cmake and, on Intel, nasm for build. |
| `lib/libarchive.rb` | libb2, lz4, xz and zstd; macOS bzip2 and expat. |
| `r/rubberband.rb` | meson, ninja, pkgconf, libsamplerate and libsndfile; selects libsamplerate. |
| `z/zimg.rb` | autoconf, automake and libtool for build. |
| `v/vulkan-loader.rb` | cmake and pkgconf for build; vulkan-headers. |
| `l/little-cms2.rb` | jpeg-turbo and libtiff. |
| `m/meson.rb` | ninja and python@3.15. |

The stable mujs 1.3.11 formula applies https://github.com/ccxvii/mujs/commit/e21c6bfdce374e19800f2455f45828a90fce39da.patch?full_index=1, SHA-256 `e10de8b9c3a62ffe121b61fe60b67ba8faa68eaace9a3b17a13f46a2cc795a11`. pkgconf's muon resource and nasm's additional build dependencies are HEAD-only, not stable requirements.

## Selected stable resources

libplacebo's Python resources are:

| Resource | Source | SHA-256 |
| --- | --- | --- |
| glad2 2.0.8 | https://files.pythonhosted.org/packages/6e/5a/d62b24fe1c7c2f34e15c2aa4418a5327a8550fdc272999a59e0dddebc3ee/glad2-2.0.8.tar.gz | `b84079b9fa404f37171b961bdd1d8da21370e6c818defb8481c5b3fe3d6436da` |
| jinja2 3.1.6 | https://files.pythonhosted.org/packages/df/bf/f7da0350254c0ed7c72f3e33cef02e048281fec7ecec5f032d4aac52226b/jinja2-3.1.6.tar.gz | `0137fb05990d35f1275a587e9aee6d56da821fc83491a0fb838183be43f66d6d` |
| markupsafe 3.0.4 | https://files.pythonhosted.org/packages/38/9b/e422a865e1d5d57d0e509b4e0bf1c1a70a7f6382c29a5aa428df994c8bc8/markupsafe-3.0.4.tar.gz | `2e9ad7dd851bf45fab9f75cbff4cb493fee9979e8d8c7c9c3ee119022518edd6` |

Vapoursynth's glslang source is https://github.com/KhronosGroup/glslang/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz, SHA-256 `cda24ec765e6d2845d106b7caa593f34127195a88524cf87821ff0088fb6edec`.

Stable shaderc resources are Git revisions, not archive SHA-256 values: `https://github.com/KhronosGroup/glslang.git` at `e1b562a8bed273a02f30b59b66a5d499793cede5`; `https://github.com/KhronosGroup/SPIRV-Headers.git` at `04fd3caa1e8267e4d95c806cad901181728e1006`; and `https://github.com/KhronosGroup/SPIRV-Tools.git` at `ef96ed763b43b59b33b31b362f09a02b729fa1c9`.

Stable MoltenVK resources are Git revisions, not archive SHA-256 values:

| Repository under `https://github.com/` | Revision |
| --- | --- |
| KhronosGroup/SPIRV-Cross.git | `6c09849fe88c48eaed08413aa022aaa136a3a057` |
| KhronosGroup/SPIRV-Headers.git | `29981f65241605e08b0ede4cfeb999fe3b723c6a` |
| KhronosGroup/SPIRV-Tools.git | `0d6fd73ca73830ccab5fa1f00ed5ed40124e2c55` |
| KhronosGroup/Vulkan-Headers.git | `e3b1eec08173d6b825cd3ac88c885a63b621504a` |
| KhronosGroup/Vulkan-Tools.git | `8c66b352925cb771f793a4d3220b1321ae0febf1` |
| USCiLab/cereal.git | `a56bad8bbb770ee266e930c95d37fff2a5be7fea` |

## Unresolved work

A static filtered traversal is not an evaluated Homebrew dependency graph. It includes build dependencies and does not establish a minimal bundle or installation order. System dependencies, nested platform conditions, implicit build downloads, resources and patches across the entire transitive graph need authoritative evaluation and acquisition. Git revisions do not supply hashes for separately generated archive bytes. Versions, URLs, hashes and patch contents for all other direct and transitive components must be recorded and verified; local Homebrew patch files and embedded `:DATA` patches also need immutable identities. The Python, GLib and gobject-introspection paths include further build resources and patches.

No Tahoe Intel build, feature equivalence, ABI compatibility, packaging, license compliance, or playback acceptance has been demonstrated. The bounded host probe selected SDK 26.5 and Apple clang 21 and passed C++ and Objective-C++ link probes. Full Xcode availability, exact build-tool compatibility, offline acquisition, complete build order, reproducibility, and bundled library relocation remain open. Related discovery is in `macos-media-source-index.md` and `macos-media-graph-discovery.md`.
