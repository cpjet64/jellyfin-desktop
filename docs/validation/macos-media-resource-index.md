# Candidate formula resources and inline patches

A conservative metadata index, not an executable lock. The first resource
declaration for a name is shown; confirm stable/platform applicability and
any nested build inputs against the pinned formula. Test resources are
marked and can be omitted only if that formula test is not required.

| Formula / resource | Source | SHA-256 or Git revision | Scope |
| --- | --- | --- | --- |
| dav1d / homebrew-00000000.ivf | `https://code.videolan.org/videolan/dav1d-test-data/raw/1.1.0/8-bit/data/00000000.ivf` | `52b4351f9bc8a876c8f3c9afc403d9e90f319c1882bfe44667d41c8c6f5486f3` | test |
| glib / gobject-introspection | `https://download.gnome.org/sources/gobject-introspection/1.86/gobject-introspection-1.86.0.tar.xz` | `920d1a3fcedeadc32acff95c2e203b319039dd4b4a08dd1a2dfd283d19c0b9ae` | review condition |
| gobject-introspection / mako | `https://files.pythonhosted.org/packages/5a/09/e07c4b5579a79f4b16f8d4f29f6c54514ac787c4ad506b8c4f28a0e6b0bf/mako-1.4.3.tar.gz` | `cd6537fe88d5fec315c55c2f8529bc4ce7a9a352ad7db3eeaa6a66e2dd4ec37a` | review condition |
| gobject-introspection / markdown | `https://files.pythonhosted.org/packages/f4/32/0b4d0297064244f04af4331b65de16237cc3cce07474171ee14222a28981/markdown-3.11.2.tar.gz` | `87bf5d90f2d2882e2c02fcda182c2800456b985bff38913867c49706481514b0` | review condition |
| gobject-introspection / markupsafe | `https://files.pythonhosted.org/packages/38/9b/e422a865e1d5d57d0e509b4e0bf1c1a70a7f6382c29a5aa428df994c8bc8/markupsafe-3.0.4.tar.gz` | `2e9ad7dd851bf45fab9f75cbff4cb493fee9979e8d8c7c9c3ee119022518edd6` | review condition |
| gobject-introspection / setuptools | `https://files.pythonhosted.org/packages/70/b3/c19426f783614d625699a4b247faf628ea9ab1db228674491767b417f9e1/setuptools-84.2.1.tar.gz` | `3ff8b3b5492a0aa66960f4419365a86b84e013070d4313e4d4a6894f5d64d6bc` | review condition |
| graphite2 / testfont | `https://scripts.sil.org/pub/woff/fonts/Simple-Graphite-Font.ttf` | `7e573896bbb40088b3a8490f83d6828fb0fd0920ac4ccdfdd7edb804e852186a` | test |
| harfbuzz / homebrew-test-ttf | `https://github.com/harfbuzz/harfbuzz/raw/fc0daafab0336b847ac14682e581a8838f36a0bf/test/shaping/fonts/sha1sum/270b89df543a7e48e206a2d830c0e10e5265c630.ttf` | `9535d35dab9e002963eef56757c46881f6b3d3b27db24eefcc80929781856c77` | test |
| help2man / Locale::gettext | `https://cpan.metacpan.org/authors/id/P/PV/PVANDRY/gettext-1.07.tar.gz` | `909d47954697e7c04218f972915b787bd1244d75e3bd01620bc167d5bbc49c15` | review condition |
| libogg / oggfile | `https://upload.wikimedia.org/wikipedia/commons/c/c8/Example.ogg` | `f57b56d8aae4c847cf01224fb45293610d801cfdac43d932b5eeab1cd318182a` | test |
| libplacebo / glad2 | `https://files.pythonhosted.org/packages/6e/5a/d62b24fe1c7c2f34e15c2aa4418a5327a8550fdc272999a59e0dddebc3ee/glad2-2.0.8.tar.gz` | `b84079b9fa404f37171b961bdd1d8da21370e6c818defb8481c5b3fe3d6436da` | review condition |
| libplacebo / jinja2 | `https://files.pythonhosted.org/packages/df/bf/f7da0350254c0ed7c72f3e33cef02e048281fec7ecec5f032d4aac52226b/jinja2-3.1.6.tar.gz` | `0137fb05990d35f1275a587e9aee6d56da821fc83491a0fb838183be43f66d6d` | review condition |
| libplacebo / markupsafe | `https://files.pythonhosted.org/packages/38/9b/e422a865e1d5d57d0e509b4e0bf1c1a70a7f6382c29a5aa428df994c8bc8/markupsafe-3.0.4.tar.gz` | `2e9ad7dd851bf45fab9f75cbff4cb493fee9979e8d8c7c9c3ee119022518edd6` | review condition |
| libpng / pngtest.png | `https://src.fedoraproject.org/rpms/libpng/raw/49e9a06ca115aaa911dd3419ee79c1870d1428fb/f/pngtest.png` | `f925a657a5343cfb724414c01e87afd4d60b1f82a46edc0e11f016a126f84064` | review condition |
| molten-vk / SPIRV-Cross | `https://github.com/KhronosGroup/SPIRV-Cross.git` | `git 6c09849fe88c48eaed08413aa022aaa136a3a057` | review condition |
| molten-vk / SPIRV-Headers | `https://github.com/KhronosGroup/SPIRV-Headers.git` | `git 29981f65241605e08b0ede4cfeb999fe3b723c6a` | review condition |
| molten-vk / SPIRV-Tools | `https://github.com/KhronosGroup/SPIRV-Tools.git` | `git 0d6fd73ca73830ccab5fa1f00ed5ed40124e2c55` | review condition |
| molten-vk / Vulkan-Headers | `https://github.com/KhronosGroup/Vulkan-Headers.git` | `git e3b1eec08173d6b825cd3ac88c885a63b621504a` | review condition |
| molten-vk / Vulkan-Tools | `https://github.com/KhronosGroup/Vulkan-Tools.git` | `git 8c66b352925cb771f793a4d3220b1321ae0febf1` | review condition |
| molten-vk / cereal | `https://github.com/USCiLab/cereal.git` | `git a56bad8bbb770ee266e930c95d37fff2a5be7fea` | review condition |
| pkgconf / muon | `https://muon.build/releases/v0.6.0/muon-v0.6.0.tar.gz` | `90a8428bc2178c59b9f7ddd1cb1cc6355f4df0c3ac023f7eefd159ae4f054024` | review condition |
| python@3.14 / flit-core | `https://files.pythonhosted.org/packages/e7/91/add211b38c357bf1b94900b4f79c34661a92be65c0243d2b0a3393c5092d/flit_core-4.1.0.tar.gz` | `62e12b63ead8335b37f59fabb977c7167fe476dafb5e41785dfa8c9aff843bc6` | review condition |
| python@3.14 / packaging | `https://files.pythonhosted.org/packages/7d/fa/3944b40b07da9ce895c0e6303a5ab7d53da063554f534556b134a54d6093/packaging-26.3.tar.gz` | `94edc256424af38762eb31306eed28beb9f0efc50a8837492c9d6fd6004aed79` | review condition |
| python@3.14 / pip | `https://files.pythonhosted.org/packages/fa/e8/0842d757e483e895e670f655084cfe8b8c72580c6769a76b30b2789a0608/pip-26.2.2.tar.gz` | `3a8e5c800492787f33a4114ef54444546c340df428974ebba17e0fd47e11af2f` | review condition |
| python@3.14 / wheel | `https://files.pythonhosted.org/packages/d0/20/50ed6bdf27dec98b568a8ae25dc599f35baa3d9709f9e83fd1edb56b9a90/wheel-0.48.0.tar.gz` | `94800765601e9171bf5d58d066e640662842bcedcbab982b2c90787a2c987322` | review condition |
| python@3.15 / flit-core | `https://files.pythonhosted.org/packages/e7/91/add211b38c357bf1b94900b4f79c34661a92be65c0243d2b0a3393c5092d/flit_core-4.1.0.tar.gz` | `62e12b63ead8335b37f59fabb977c7167fe476dafb5e41785dfa8c9aff843bc6` | review condition |
| python@3.15 / pip | `https://files.pythonhosted.org/packages/fa/e8/0842d757e483e895e670f655084cfe8b8c72580c6769a76b30b2789a0608/pip-26.2.2.tar.gz` | `3a8e5c800492787f33a4114ef54444546c340df428974ebba17e0fd47e11af2f` | review condition |
| rust / rustc-bootstrap | `https://static.rust-lang.org/dist/2026-08-20/rustc-1.98.0-x86_64-apple-darwin.tar.xz` | `c82d8f536955a9d6fc4465637fce5dcacf1d3913a98b5eb7edd9ead5a8b3f509` | review condition |
| rust / cargo-bootstrap | `https://static.rust-lang.org/dist/2026-08-20/cargo-1.98.0-x86_64-apple-darwin.tar.xz` | `3526be8588e7f80cf0604ce184dd8af89798786e389d5deea4ae4ffe2f104265` | review condition |
| rust / rust-std-bootstrap | `https://static.rust-lang.org/dist/2026-08-20/rust-std-1.98.0-x86_64-apple-darwin.tar.xz` | `8923fa9d0e0407b8a492e59d568e7aceff75949e1eee2e3e1b60e174373890cb` | review condition |
| shaderc / glslang | `https://github.com/KhronosGroup/glslang.git` | `git e1b562a8bed273a02f30b59b66a5d499793cede5` | review condition |
| shaderc / spirv-headers | `https://github.com/KhronosGroup/SPIRV-Headers.git` | `git 04fd3caa1e8267e4d95c806cad901181728e1006` | review condition |
| shaderc / spirv-tools | `https://github.com/KhronosGroup/SPIRV-Tools.git` | `git ef96ed763b43b59b33b31b362f09a02b729fa1c9` | review condition |
| svt-av1 / homebrew-testvideo | `https://github.com/grusell/svt-av1-homebrew-testdata/raw/main/video_64x64_yuv420p_25frames.yuv` | `0c5cc90b079d0d9c1ded1376357d23a9782a704a83e01731f50ccd162e246492` | test |
| vapoursynth / glslang | `https://github.com/KhronosGroup/glslang/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz` | `cda24ec765e6d2845d106b7caa593f34127195a88524cf87821ff0088fb6edec` | review condition |
| x265 / homebrew-test_video | `https://raw.githubusercontent.com/fraunhoferhhi/vvenc/master/test/data/RTn23_80x44p15_f15.yuv` | `ecd2ef466dd2975f4facc889e0ca128a6bea6645df61493a96d8e7763b6f3ae9` | test |
| yt-dlp / brotli | `https://files.pythonhosted.org/packages/cf/81/1546c983678a9b4e77cb4718eaedd1b2caf24a6cdbf1eb8775938d22aff7/brotli-1.2.1.tar.gz` | `02b2f7b5a83485efe448a44ddf7983def05c3bb1dcf917d4f7154070260e13de` | review condition |
| yt-dlp / charset-normalizer | `https://files.pythonhosted.org/packages/33/1c/f41d4e74c28ab327ff3acd36053f7ea506c55872d7a90b0fa71aa3ab0c89/charset_normalizer-3.5.2.tar.gz` | `39de2a259fc954455c57274dc94c79d5842774e1247a016aff30bc0efed0f4ef` | review condition |
| yt-dlp / curl-cffi | `https://files.pythonhosted.org/packages/96/4c/69074f9797d83a5401b3c6bc58c5de5aad6d1a085d0497b87a093a152e87/curl_cffi-0.17.0.tar.gz` | `31ee8500d06d6502ce1ec0b7ded1c5aa8e8f3401bbe37ccd78d6153d859d8f3c` | review condition |
| yt-dlp / idna | `https://files.pythonhosted.org/packages/f5/08/8eea9d4b8302028f3abb2c0813953f7aec26d33b7a8960ed760e65ff29fa/idna-3.20.tar.gz` | `a7db850025b95ded1eae8a46181a1a6c56c92c96f0e2b005d9ff8dc0210cab44` | review condition |
| yt-dlp / mutagen | `https://files.pythonhosted.org/packages/df/70/1675da133ea92227da41bf5b24e1c66be597ff736a1533ade41da986852f/mutagen-1.48.1.tar.gz` | `8f95637ab9f6f305cec6bd1294e197debe207998e3e068596563c74f86b0a173` | review condition |
| yt-dlp / pycryptodomex | `https://files.pythonhosted.org/packages/f4/e7/50e72301e32acb73a314e663c2014e1ddefc019d5f434ff14ec89861d664/pycryptodomex-3.24.1.tar.gz` | `09081666ffc599976c0b8b29caf2cf82212c0f05bed233c8f8ed53e4f6e1d356` | review condition |
| yt-dlp / requests | `https://files.pythonhosted.org/packages/ac/c3/e2a2b89f2d3e2179abd6d00ebd70bff6273f37fb3e0cc209f48b39d00cbf/requests-2.34.2.tar.gz` | `f288924cae4e29463698d6d60bc6a4da69c89185ad1e0bcc4104f584e960b9ed` | review condition |
| yt-dlp / urllib3 | `https://files.pythonhosted.org/packages/e3/05/b17359e1cefb4f909b5e40b1b90a496d987258916dbbf88e842c729f510e/urllib3-2.8.0.tar.gz` | `63bf2ead4c879426ebf22ef2a781eeb4aa3b4ae798a0435506f8687fd5bb9b63` | review condition |
| yt-dlp / websockets | `https://files.pythonhosted.org/packages/30/4b/4692be6d5bf739ed049b3813901c42091f8c054df89d5c089343823dea0d/websockets-17.3.tar.gz` | `1c7dd93a1c9f89297471493674b1fc1d9467cafd45374f2172213046bdb3a329` | review condition |
| yt-dlp / yt-dlp-ejs | `https://files.pythonhosted.org/packages/d3/e6/cceb9530e8f4e5940f6f7822d90e9d94f1b85343329a16baaf47bbbb3de1/yt_dlp_ejs-0.8.0.tar.gz` | `d5fa1639f63b5c4af8d932495f60689d5370f1a095782c944f7f62a303eb104e` | review condition |

## Inline patches

Hash bytes after the `__END__` marker and its LF;
preserve the final newline. Applicability remains to be checked.

- gobject-introspection: 1795 bytes, SHA-256 `4cbe63e79c1ee9cd8e15250196723a4e095b3e6bd2c717c38bb797e638d73200`.
- libvpx: 1720 bytes, SHA-256 `8821af8076eeacbe8d9055ef3b6c95c7eeef7e7ff8b5e13c04520c75642b1dce`.
