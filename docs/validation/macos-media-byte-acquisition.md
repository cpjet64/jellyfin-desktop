# Proposed bounded source-byte acquisition

This is a review proposal only. No source archives have been downloaded. These
ten stable Git revisions have no archive SHA-256 in the pinned reference.
The URLs below propose tar archives for those exact commits, not moving tags.
An HTTP HEAD request on 2026-11-04 returned 200 for each; GitHub did not report
Content-Length, and the x264 response reported zero, which is not a usable size.
Actual compressed and expanded sizes remain unknown.

| Source | Exact proposed URL | Compressed cap |
| --- | --- | ---: |
| x264 | https://code.videolan.org/videolan/x264/-/archive/b35605ace3ddf7c1a5d67a2eb553f034aef41d55/x264-b35605ace3ddf7c1a5d67a2eb553f034aef41d55.tar.gz | 32 MiB |
| SPIRV-Cross | https://github.com/KhronosGroup/SPIRV-Cross/archive/6c09849fe88c48eaed08413aa022aaa136a3a057.tar.gz | 32 MiB |
| SPIRV-Headers | https://github.com/KhronosGroup/SPIRV-Headers/archive/29981f65241605e08b0ede4cfeb999fe3b723c6a.tar.gz | 16 MiB |
| SPIRV-Tools | https://github.com/KhronosGroup/SPIRV-Tools/archive/0d6fd73ca73830ccab5fa1f00ed5ed40124e2c55.tar.gz | 64 MiB |
| Vulkan-Headers | https://github.com/KhronosGroup/Vulkan-Headers/archive/e3b1eec08173d6b825cd3ac88c885a63b621504a.tar.gz | 32 MiB |
| Vulkan-Tools | https://github.com/KhronosGroup/Vulkan-Tools/archive/8c66b352925cb771f793a4d3220b1321ae0febf1.tar.gz | 32 MiB |
| cereal | https://github.com/USCiLab/cereal/archive/a56bad8bbb770ee266e930c95d37fff2a5be7fea.tar.gz | 16 MiB |
| shaderc glslang | https://github.com/KhronosGroup/glslang/archive/e1b562a8bed273a02f30b59b66a5d499793cede5.tar.gz | 64 MiB |
| shaderc SPIRV-Headers | https://github.com/KhronosGroup/SPIRV-Headers/archive/04fd3caa1e8267e4d95c806cad901181728e1006.tar.gz | 16 MiB |
| shaderc SPIRV-Tools | https://github.com/KhronosGroup/SPIRV-Tools/archive/ef96ed763b43b59b33b31b362f09a02b729fa1c9.tar.gz | 64 MiB |

The proposed aggregate compressed limit is 368 MiB; each limit is a hard
streaming stop, not a prediction. First verify free space and write only to a
new task-owned ignored path inside the project. Stop on capacity errors,
unexpected content, a redirect outside the named official provider, or a cap.
Record the final URL, status, byte count and SHA-256 of each exact archive;
independently check archive content corresponds to the specified Git commit
using trusted upstream Git object evidence. A newly computed SHA-256 records
the acquired bytes and is not independent authentication by itself. Review
and approve the resulting lock before use; never silently replace a hash after
an upstream archive changes. Do not extract or build during this acquisition.
An archive URL's content is not presumed byte-identical to Homebrew's git
checkout. Additional unidentified inputs are not covered by these ten caps.
The pinned core Python, glib and libtool patches reside in homebrew-core itself.
Their SHA-256 values at this reference, respectively, are
`9f2eae1d08720b06ac3d9ef1999c09388b9db39dfb52687fc261ff820bff20c3`,
`d846efd0bf62918350da94f850db33b0f8727fece9bfaf8164566e3094e80c97` and
`35acd6aebc19843f1a2b3a63e880baceb0f5278ab1ace661e57a502d9d78c93c`.
They are `Patches/python/3.13-sysconfig.diff`,
`Patches/glib/hardcoded-paths.diff` and
`Patches/libtool/configure-big_sur.diff` in the pinned core tree.

The inline `libvpx` patch is already present in the pinned formula's `__END__`
section. Its 1,720 UTF-8 bytes after the `__END__` line, including final LF,
have SHA-256 `8821af8076eeacbe8d9055ef3b6c95c7eeef7e7ff8b5e13c04520c75642b1dce`.
The digest is for those exact bytes, not a platform-normalized copy.
