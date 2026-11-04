# Tahoe candidate dependency graph and ordering

This is a conservative static declaration walk, not an executable lock.
Each arrow points from dependent to its declared dependencies. Conditions,
system fallbacks, resources, build flags and cycles require resolution.
Host CMake, Ninja and pkgconf are treated as external tools. Meson is
an external interpreter consumer; Python compatibility is unverified.
Only mpv HTML documentation generation is proposed for exclusion.

## Candidate dependency edges

- **autoconf** → m4 [runtime]
- **automake** → autoconf [runtime]
- **bison** → no quoted formula dependency retained
- **brotli** → cmake [build/test]
- **ca-certificates** → no quoted formula dependency retained
- **cairo** → meson [build/test], ninja [build/test], pkgconf [build/test], fontconfig [runtime], freetype [runtime], glib [runtime], libpng [runtime], libx11 [runtime], libxcb [runtime], libxext [runtime], libxrender [runtime], lzo [runtime], pixman [runtime], gettext [runtime] (macOS or architecture condition)
- **certifi** → python-setuptools [build/test], python@3.15 [build/test], ca-certificates [no-linkage]
- **cffi** → python@3.14 [build/test], python@3.15 [build/test], pycparser [runtime]
- **cmake** → no quoted formula dependency retained
- **dav1d** → meson [build/test], ninja [build/test], nasm [build/test] (macOS or architecture condition)
- **deno** → cmake [build/test], lld [build/test], llvm [build/test], ninja [build/test], pkgconf [build/test], rust [build/test], little-cms2 [runtime], sqlite [runtime]
- **fast_float** → cmake [build/test]
- **ffmpeg** → pkgconf [build/test], dav1d [runtime], lame [runtime], libvmaf [runtime], libvpx [runtime], openssl@4 [runtime], opus [runtime], sdl2-compat [runtime], svt-av1 [runtime], x264 [runtime], x265 [runtime], xz [runtime], nasm [build/test] (macOS or architecture condition)
- **flac** → pkgconf [build/test], libogg [runtime]
- **fontconfig** → gettext [build/test], meson [build/test], ninja [build/test], pkgconf [build/test], freetype [runtime], gettext [runtime] (macOS or architecture condition)
- **freetype** → pkgconf [build/test], brotli [runtime], libpng [runtime]
- **fribidi** → help2man [build/test], pkgconf [build/test]
- **gettext** → json-c [runtime], libunistring [runtime]
- **giflib** → no quoted formula dependency retained
- **glib** → bison [build/test], gettext [build/test], meson [build/test], ninja [build/test], pkgconf [build/test], python-setuptools [build/test], python@3.15 [build/test], pcre2 [runtime], gettext [runtime] (macOS or architecture condition)
- **gobject-introspection** → bison [build/test], meson [build/test], ninja [build/test], cairo [runtime], glib [runtime], pkgconf [runtime], python@3.15 [runtime]
- **graphite2** → cmake [build/test]
- **harfbuzz** → gobject-introspection [build/test], meson [build/test], ninja [build/test], pkgconf [build/test], python@3.15 [build/test], cairo [runtime], freetype [runtime], glib [runtime], graphite2 [runtime], icu4c@79 [runtime], libpng [runtime]
- **help2man** → gettext [runtime]
- **icu4c@79** → no quoted formula dependency retained
- **jpeg-turbo** → cmake [build/test], nasm [build/test] (macOS or architecture condition)
- **json-c** → cmake [build/test]
- **lame** → pkgconf [build/test], mpg123 [runtime]
- **libarchive** → libb2 [runtime], lz4 [runtime], xz [runtime], zstd [runtime]
- **libass** → pkgconf [build/test], freetype [runtime], fribidi [runtime], harfbuzz [runtime], libunibreak [runtime], nasm [build/test] (macOS or architecture condition)
- **libb2** → no quoted formula dependency retained
- **libbluray** → meson [build/test], ninja [build/test], pkgconf [build/test], fontconfig [runtime], freetype [runtime], libudfread [runtime]
- **libgit2** → cmake [build/test], pkgconf [build/test], libssh2 [runtime], llhttp [runtime], openssl@4 [runtime]
- **libogg** → cmake [build/test]
- **libplacebo** → fast_float [build/test], meson [build/test], ninja [build/test], pkgconf [build/test], python@3.15 [build/test], vulkan-headers [build/test], little-cms2 [runtime], shaderc [runtime], vulkan-loader [runtime]
- **libpng** → no quoted formula dependency retained
- **libsamplerate** → cmake [build/test], pkgconf [build/test]
- **libsndfile** → cmake [build/test], flac [runtime], lame [runtime], libogg [runtime], libvorbis [runtime], mpg123 [runtime], opus [runtime]
- **libssh2** → openssl@4 [runtime]
- **libtiff** → jpeg-turbo [runtime], webp [runtime], xz [runtime], zstd [runtime]
- **libtool** → m4 [runtime]
- **libudfread** → meson [build/test], ninja [build/test]
- **libunibreak** → no quoted formula dependency retained
- **libunistring** → no quoted formula dependency retained
- **libvmaf** → meson [build/test], ninja [build/test], nasm [build/test] (macOS or architecture condition)
- **libvorbis** → pkgconf [build/test], libogg [runtime]
- **libvpx** → nasm [build/test] (macOS or architecture condition)
- **libx11** → pkgconf [build/test], util-macros [build/test], xtrans [build/test], libxcb [runtime], xorgproto [runtime]
- **libxau** → pkgconf [build/test], util-macros [build/test], xorgproto [runtime]
- **libxcb** → pkgconf [build/test], python@3.15 [build/test], xcb-proto [build/test], libxau [runtime], libxdmcp [runtime]
- **libxdmcp** → pkgconf [build/test], xorgproto [runtime]
- **libxext** → pkgconf [build/test], libx11 [runtime], xorgproto [runtime]
- **libxrender** → pkgconf [build/test], libx11 [runtime], xorgproto [runtime]
- **little-cms2** → jpeg-turbo [runtime], libtiff [runtime]
- **lld** → cmake [build/test], llvm [runtime], zstd [runtime]
- **llhttp** → cmake [build/test]
- **llvm** → cmake [build/test], ninja [build/test], python@3.15 [build/test], zstd [runtime], z3 [runtime] (macOS or architecture condition)
- **luajit** → no quoted formula dependency retained
- **lz4** → cmake [build/test]
- **lzo** → no quoted formula dependency retained
- **m4** → no quoted formula dependency retained
- **meson** → no quoted formula dependency retained
- **molten-vk** → cmake [build/test]
- **mpdecimal** → no quoted formula dependency retained
- **mpg123** → no quoted formula dependency retained
- **mpv** → meson [build/test], ninja [build/test], pkgconf [build/test], ffmpeg [runtime], jpeg-turbo [runtime], libarchive [runtime], libass [runtime], libbluray [runtime], libplacebo [runtime], little-cms2 [runtime], luajit [runtime], mujs [runtime], rubberband [runtime], uchardet [runtime], vapoursynth [runtime], vulkan-loader [runtime], yt-dlp [runtime], zimg [runtime], molten-vk [runtime] (macOS or architecture condition)
- **mujs** → no quoted formula dependency retained
- **nasm** → no quoted formula dependency retained
- **ninja** → no quoted formula dependency retained
- **openssl@4** → ca-certificates [no-linkage]
- **opus** → no quoted formula dependency retained
- **pcre2** → no quoted formula dependency retained
- **pixman** → meson [build/test], ninja [build/test]
- **pkgconf** → no quoted formula dependency retained
- **pycparser** → python-setuptools [build/test], python@3.15 [build/test]
- **python-setuptools** → python@3.15 [build/test]
- **python@3.14** → pkgconf [build/test], ca-certificates [no-linkage], mpdecimal [runtime], openssl@4 [runtime], sqlite [runtime], xz [runtime], zstd [runtime]
- **python@3.15** → pkgconf [build/test], ca-certificates [no-linkage], mpdecimal [runtime], openssl@4 [runtime], sqlite [runtime], xz [runtime], zstd [runtime]
- **readline** → no quoted formula dependency retained
- **rubberband** → meson [build/test], ninja [build/test], pkgconf [build/test], libsamplerate [runtime], libsndfile [runtime]
- **rust** → libgit2 [runtime], libssh2 [runtime], llvm [runtime], openssl@4 [runtime], pkgconf [runtime], sqlite [runtime]
- **sdl2-compat** → cmake [build/test], sdl3 [no-linkage]
- **sdl3** → cmake [build/test], pkgconf [build/test]
- **shaderc** → cmake [build/test]
- **sqlite** → readline [runtime]
- **svt-av1** → cmake [build/test], nasm [build/test] (macOS or architecture condition)
- **uchardet** → cmake [build/test]
- **util-macros** → no quoted formula dependency retained
- **vapoursynth** → ninja [build/test], pkgconf [build/test], vulkan-headers [build/test], python@3.15 [runtime], zimg [runtime]
- **vulkan-headers** → cmake [build/test]
- **vulkan-loader** → cmake [build/test], pkgconf [build/test], vulkan-headers [runtime]
- **webp** → cmake [build/test], giflib [runtime], jpeg-turbo [runtime], libpng [runtime]
- **x264** → nasm [build/test] (macOS or architecture condition)
- **x265** → cmake [build/test], nasm [build/test] (macOS or architecture condition)
- **xcb-proto** → pkgconf [build/test], python@3.15 [build/test]
- **xorgproto** → pkgconf [build/test], util-macros [build/test]
- **xtrans** → pkgconf [build/test], util-macros [build/test]
- **xz** → no quoted formula dependency retained
- **yt-dlp** → pkgconf [build/test], certifi [runtime], cffi [runtime], deno [runtime], pycparser [runtime], python@3.15 [runtime]
- **z3** → cmake [build/test], python@3.15 [build/test]
- **zimg** → autoconf [build/test], automake [build/test], libtool [build/test]
- **zstd** → cmake [build/test], lz4 [runtime], xz [runtime]

## Dependency-first candidate groups

The groups are generated from the edges above; a group with more than
one member is a cycle, not an approved build sequence. Host/system
prerequisites and resources are not represented by this ordering.

1. m4
2. autoconf
3. automake
4. bison
5. cmake
6. brotli
7. ca-certificates
8. libpng
9. pkgconf
10. freetype
11. json-c
12. libunistring
13. gettext
14. meson
15. ninja
16. fontconfig
17. pcre2
18. mpdecimal
19. openssl@4
20. readline
21. sqlite
22. xz
23. lz4
24. zstd
25. python@3.15
26. python-setuptools
27. glib
28. util-macros
29. xorgproto
30. libxau
31. libxdmcp
32. xcb-proto
33. libxcb
34. xtrans
35. libx11
36. libxext
37. libxrender
38. lzo
39. pixman
40. cairo
41. certifi
42. pycparser
43. python@3.14
44. cffi
45. nasm
46. dav1d
47. jpeg-turbo
48. giflib
49. webp
50. libtiff
51. little-cms2
52. z3
53. llvm
54. lld
55. libssh2
56. llhttp
57. libgit2
58. rust
59. deno
60. fast_float
61. mpg123
62. lame
63. libvmaf
64. libvpx
65. opus
66. sdl3
67. sdl2-compat
68. svt-av1
69. x264
70. x265
71. ffmpeg
72. libogg
73. flac
74. help2man
75. fribidi
76. gobject-introspection
77. graphite2
78. icu4c@79
79. harfbuzz
80. libb2
81. libarchive
82. libunibreak
83. libass
84. libudfread
85. libbluray
86. shaderc
87. vulkan-headers
88. vulkan-loader
89. libplacebo
90. libsamplerate
91. libvorbis
92. libsndfile
93. libtool
94. luajit
95. molten-vk
96. mujs
97. rubberband
98. uchardet
99. zimg
100. vapoursynth
101. yt-dlp
102. mpv
