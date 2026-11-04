# FFmpeg source components for macOS Tahoe

This is a metadata inventory, not a tested build recipe or a completed source lock. It records stable formula declarations from Homebrew core commit `c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5` for Intel macOS Tahoe. Source hashes below are SHA-256. No archives were downloaded or builds run for this inventory. Formula paths are under [the pinned Homebrew core tree](https://github.com/Homebrew/homebrew-core/tree/c38a4a58b06912c3e64e5afd7f7dfe8aedd84dc5/Formula); names beginning with `lib` reside in `Formula/lib/`.

## Proposed source inputs

| Component | Source | SHA-256 or Git revision |
| --- | --- | --- |
| FFmpeg 9.0.2, formula revision 1 | https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz | `8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e` |
| dav1d 1.5.4 | https://code.videolan.org/videolan/dav1d/-/archive/1.5.4/dav1d-1.5.4.tar.bz2 | `2abfb0c89212e6e4733a54e0ae509ec00a5b845a6360946f918806e14aedb011` |
| LAME 4.0 | https://downloads.sourceforge.net/project/lame/lame/4.0/lame-4.0.tar.gz | `3df5124d5ad3a98312ffd7ba6a9b36230e4f8a3e66d3ce0f425e336c32d216eb` |
| libvmaf 3.2.1 | https://github.com/Netflix/vmaf/archive/refs/tags/v3.2.1.tar.gz | `5df7386911bc15fd1ca783132528748d219768ae4fc5f8e0b61184f041648092` |
| libvpx 1.17.0 | https://github.com/webmproject/libvpx/archive/refs/tags/v1.17.0.tar.gz | `1020f184046187baa2985dbde38e0691f49c44088bca7a1842b0236c6081dc0a` |
| OpenSSL 4.1.0 | https://github.com/openssl/openssl/releases/download/openssl-4.1.0/openssl-4.1.0.tar.gz | `50c17baf54cb008f6ce42d50deb4b28050a1aad711636cabd4b241c7e90cc2fd` |
| Opus 1.6.1 | https://ftp.osuosl.org/pub/xiph/releases/opus/opus-1.6.1.tar.gz | `6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1` |
| SDL2 compatibility 2.32.74 | https://github.com/libsdl-org/sdl2-compat/releases/download/release-2.32.74/sdl2-compat-2.32.74.tar.gz | `ec68abde77e2e459c8abc4f9587b8976b5d2a657b2bfda2fb3a079b0f8924588` |
| SDL3 3.4.20 | https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-3.4.20.tar.gz | `f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f` |
| SVT-AV1 4.3.0 | https://gitlab.com/AOMediaCodec/SVT-AV1/-/archive/v4.3.0/SVT-AV1-v4.3.0.tar.bz2 | `9223ce06e6172ad41357ed23b3b00f036e03794b86b0e20ea9ccb3f648b8955d` |
| x264 r3222 | https://code.videolan.org/videolan/x264.git | Git revision `b35605ace3ddf7c1a5d67a2eb553f034aef41d55`; no archive hash declared |
| x265 4.3 | https://github.com/Multicorewareinc/x265/releases/download/4.3/x265_4.3.tar.gz | `83c53e4c8bbb8f1e33ed59e10a7d621d1d7801ca853910c3eb41f038b8ffb121` |
| xz 5.8.4 | https://github.com/tukaani-project/xz/releases/download/v5.8.4/xz-5.8.4.tar.gz | `0014c7886930454fe8bd4228665b51af55eeae560ea135c9c4cd33f55b2591d9` |
| mpg123 1.33.7 | https://www.mpg123.de/download/mpg123-1.33.7.tar.bz2 | `31d0e35a4ca567ec9b5ebda6c3062bb4435d6d3eacd6ef0d95cadd7854dc03ee` |
| NASM 3.02, Intel build tool | https://www.nasm.us/pub/nasm/releasebuilds/3.02/nasm-3.02.tar.xz | `87336eba53b4acfe917424ab5d500d2b0054d9f5148d35c2273ccf2cfb712f0d` |
| Meson 1.12.1, build tool | https://github.com/mesonbuild/meson/releases/download/1.12.1/meson-1.12.1.tar.gz | `ab0a6ca09f8ef70c564c8241fb5a23957886a0b53fb58412b5e07eaf07dba743` |
| CA certificates, 2026-10-20 | https://curl.se/ca/cacert-2026-10-20.pem | `796f1389bfea13c0ed78f663b765baaff54a508e5483786bd6ece73c5e3e3cbe` |

The pinned libvpx formula applies its inline `patch :DATA` to extend Darwin target support. The patch bytes are part of that pinned formula and must be retained and applied or an equivalent choice justified. The `pkgconf` formula's muon resource is in its `head` block and is not a stable-build input. dav1d, SVT-AV1, and x265 declare test-only resources; those are outside this build-input list and must be separately locked if formula tests are required.

## Graph and order

The formula declares FFmpeg dependencies on pkgconf (build), dav1d, LAME, libvmaf, libvpx, OpenSSL, Opus, SDL2 compatibility, SVT-AV1, x264, x265, xz and, on Intel, NASM (build). It uses macOS bzip2 and libxml2. dav1d and libvmaf need Meson, Ninja and Intel NASM; libvpx and x264 need Intel NASM; SVT-AV1 and x265 need CMake and Intel NASM. LAME also declares mpg123 and build-time pkgconf. SDL2 compatibility declares build-time CMake and SDL3 with `no_linkage`; SDL3 declares build-time CMake and pkgconf. OpenSSL declares CA certificates with `no_linkage` and uses macOS Perl for its build. libvmaf uses macOS `xxd` from vim. Meson requires Ninja and Python.

For the proposed custom build, first verify host prerequisites, then prepare NASM and Meson. Build mpg123 before LAME and SDL3 before SDL2 compatibility. The remaining codec libraries can be built after their build tools; build FFmpeg last against the intended prefix. The certificate bundle is data, not a compiled library; its installation and TLS trust path require a separate decision. This order follows declared dependencies and is not proof of a working build.

The formula enables shared libraries, pthreads, GPL and version 3, ffplay, libsvtav1, libopus, libx264, libmp3lame, libdav1d, libvmaf, libvpx, libx265, OpenSSL, VideoToolbox and AudioToolbox. It builds and installs additional FFmpeg tools. These options entail licensing and distribution obligations that must be assessed for the intended product. Apply the actual pinned formula's platform and linker conditions rather than treating this summary as a complete command line.

## Host prerequisites and unresolved evidence

The proposed build assumes suitable host CMake, Ninja and pkgconf, plus host Python for Meson, rather than building their Homebrew dependency trees. It also requires the Apple compiler and Tahoe SDK, make, Perl, shell and archive tools, Git, and the macOS libraries and tools noted above. Record exact versions, executable locations, architecture, SDK and deployment target on the actual host and verify compatibility. These substitutions create a custom build and do not establish byte or dependency equivalence to Homebrew.

Before claiming a complete lock or runnable artifact, resolve source acquisition and immutable storage, Git checkout identity, patch bytes, configure options and install prefix, compiler and tool versions, certificate trust, runtime SDL3 loading and relocation, licensing, transitive dynamic dependencies, and acceptance tests. Verify each downloaded source hash and the resulting artifact and dependencies on the target host. Host availability, build success, runtime behavior and license compliance have not been verified by this metadata inventory.
