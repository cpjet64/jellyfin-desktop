# Tahoe library generator and resource inputs

Resource candidates from the pinned Homebrew core stable formulae,
excluding test-only resources, pkgconf HEAD-only muon, Linux-only libpng
test data and the separate helper. Confirm build integration and hashes
before use. Git revisions do not establish archive byte hashes.

| Component / resource | Source | SHA-256 or Git revision | Scope |
| --- | --- | --- | --- |
| glib / gobject-introspection | `https://download.gnome.org/sources/gobject-introspection/1.86/gobject-introspection-1.86.0.tar.xz` | `920d1a3fcedeadc32acff95c2e203b319039dd4b4a08dd1a2dfd283d19c0b9ae` | review condition |
| gobject-introspection / mako | `https://files.pythonhosted.org/packages/5a/09/e07c4b5579a79f4b16f8d4f29f6c54514ac787c4ad506b8c4f28a0e6b0bf/mako-1.4.3.tar.gz` | `cd6537fe88d5fec315c55c2f8529bc4ce7a9a352ad7db3eeaa6a66e2dd4ec37a` | review condition |
| gobject-introspection / markdown | `https://files.pythonhosted.org/packages/f4/32/0b4d0297064244f04af4331b65de16237cc3cce07474171ee14222a28981/markdown-3.11.2.tar.gz` | `87bf5d90f2d2882e2c02fcda182c2800456b985bff38913867c49706481514b0` | review condition |
| gobject-introspection / markupsafe | `https://files.pythonhosted.org/packages/38/9b/e422a865e1d5d57d0e509b4e0bf1c1a70a7f6382c29a5aa428df994c8bc8/markupsafe-3.0.4.tar.gz` | `2e9ad7dd851bf45fab9f75cbff4cb493fee9979e8d8c7c9c3ee119022518edd6` | review condition |
| gobject-introspection / setuptools | `https://files.pythonhosted.org/packages/70/b3/c19426f783614d625699a4b247faf628ea9ab1db228674491767b417f9e1/setuptools-84.2.1.tar.gz` | `3ff8b3b5492a0aa66960f4419365a86b84e013070d4313e4d4a6894f5d64d6bc` | review condition |
| help2man / Locale::gettext | `https://cpan.metacpan.org/authors/id/P/PV/PVANDRY/gettext-1.07.tar.gz` | `909d47954697e7c04218f972915b787bd1244d75e3bd01620bc167d5bbc49c15` | review condition |
| libplacebo / glad2 | `https://files.pythonhosted.org/packages/6e/5a/d62b24fe1c7c2f34e15c2aa4418a5327a8550fdc272999a59e0dddebc3ee/glad2-2.0.8.tar.gz` | `b84079b9fa404f37171b961bdd1d8da21370e6c818defb8481c5b3fe3d6436da` | review condition |
| libplacebo / jinja2 | `https://files.pythonhosted.org/packages/df/bf/f7da0350254c0ed7c72f3e33cef02e048281fec7ecec5f032d4aac52226b/jinja2-3.1.6.tar.gz` | `0137fb05990d35f1275a587e9aee6d56da821fc83491a0fb838183be43f66d6d` | review condition |
| libplacebo / markupsafe | `https://files.pythonhosted.org/packages/38/9b/e422a865e1d5d57d0e509b4e0bf1c1a70a7f6382c29a5aa428df994c8bc8/markupsafe-3.0.4.tar.gz` | `2e9ad7dd851bf45fab9f75cbff4cb493fee9979e8d8c7c9c3ee119022518edd6` | review condition |
| molten-vk / SPIRV-Cross | `https://github.com/KhronosGroup/SPIRV-Cross.git` | `git 6c09849fe88c48eaed08413aa022aaa136a3a057` | review condition |
| molten-vk / SPIRV-Headers | `https://github.com/KhronosGroup/SPIRV-Headers.git` | `git 29981f65241605e08b0ede4cfeb999fe3b723c6a` | review condition |
| molten-vk / SPIRV-Tools | `https://github.com/KhronosGroup/SPIRV-Tools.git` | `git 0d6fd73ca73830ccab5fa1f00ed5ed40124e2c55` | review condition |
| molten-vk / Vulkan-Headers | `https://github.com/KhronosGroup/Vulkan-Headers.git` | `git e3b1eec08173d6b825cd3ac88c885a63b621504a` | review condition |
| molten-vk / Vulkan-Tools | `https://github.com/KhronosGroup/Vulkan-Tools.git` | `git 8c66b352925cb771f793a4d3220b1321ae0febf1` | review condition |
| molten-vk / cereal | `https://github.com/USCiLab/cereal.git` | `git a56bad8bbb770ee266e930c95d37fff2a5be7fea` | review condition |
| python@3.15 / flit-core | `https://files.pythonhosted.org/packages/e7/91/add211b38c357bf1b94900b4f79c34661a92be65c0243d2b0a3393c5092d/flit_core-4.1.0.tar.gz` | `62e12b63ead8335b37f59fabb977c7167fe476dafb5e41785dfa8c9aff843bc6` | review condition |
| python@3.15 / pip | `https://files.pythonhosted.org/packages/fa/e8/0842d757e483e895e670f655084cfe8b8c72580c6769a76b30b2789a0608/pip-26.2.2.tar.gz` | `3a8e5c800492787f33a4114ef54444546c340df428974ebba17e0fd47e11af2f` | review condition |
| shaderc / glslang | `https://github.com/KhronosGroup/glslang.git` | `git e1b562a8bed273a02f30b59b66a5d499793cede5` | review condition |
| shaderc / spirv-headers | `https://github.com/KhronosGroup/SPIRV-Headers.git` | `git 04fd3caa1e8267e4d95c806cad901181728e1006` | review condition |
| shaderc / spirv-tools | `https://github.com/KhronosGroup/SPIRV-Tools.git` | `git ef96ed763b43b59b33b31b362f09a02b729fa1c9` | review condition |
| vapoursynth / glslang | `https://github.com/KhronosGroup/glslang/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz` | `cda24ec765e6d2845d106b7caa593f34127195a88524cf87821ff0088fb6edec` | review condition |

Inline formula patches: gobject-introspection 1,795 bytes SHA-256
`4cbe63e79c1ee9cd8e15250196723a4e095b3e6bd2c717c38bb797e638d73200`;
libvpx 1,720 bytes SHA-256
`8821af8076eeacbe8d9055ef3b6c95c7eeef7e7ff8b5e13c04520c75642b1dce`.
These hashes cover bytes after the `__END__` line and its LF, through
the final LF. The local and remote patch indexes list the other inputs.
