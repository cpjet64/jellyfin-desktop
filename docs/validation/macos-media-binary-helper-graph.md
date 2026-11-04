# Tahoe candidate dependency graph and ordering with binary helper

This is a conservative static declaration walk, not an executable lock.
Each arrow points from dependent to its declared dependencies. Conditions,
system fallbacks, resources, build flags and cycles require resolution.
Host CMake, Ninja and pkgconf are treated as external tools. Meson is
an external interpreter consumer; Python compatibility is unverified.
Only mpv HTML documentation generation is proposed for exclusion.
yt-dlp is supplied by the separately proposed binary helper; this is not source parity.

## Candidate dependency edges

- **autoconf** → m4 [runtime]
- **automake** → autoconf [runtime]
- **bison** → no quoted formula dependency retained
- **brotli** → cmake [build/test]
- **ca-certificates** → no quoted formula dependency retained
- **cairo** → meson [build/test], ninja [build/test], pkgconf [build/test], fontconfig [runtime], freetype [runtime], glib [runtime], libpng [runtime], libx11 [runtime], libxcb [runtime], libxext [runtime], libxrender [runtime], lzo [runtime], pixman [runtime], gettext [runtime] (macOS or architecture condition)
- **cmake** → no quoted formula dependency retained
- **dav1d** → meson [build/test], ninja [build/test], nasm [build/test] (macOS or architecture condition)
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
- **libogg** → cmake [build/test]
- **libplacebo** → fast_float [build/test], meson [build/test], ninja [build/test], pkgconf [build/test], python@3.15 [build/test], vulkan-headers [build/test], little-cms2 [runtime], shaderc [runtime], vulkan-loader [runtime]
- **libpng** → no quoted formula dependency retained
- **libsamplerate** → cmake [build/test], pkgconf [build/test]
- **libsndfile** → cmake [build/test], flac [runtime], lame [runtime], libogg [runtime], libvorbis [runtime], mpg123 [runtime], opus [runtime]
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
- **luajit** → no quoted formula dependency retained
- **lz4** → cmake [build/test]
- **lzo** → no quoted formula dependency retained
- **m4** → no quoted formula dependency retained
- **meson** → no quoted formula dependency retained
- **molten-vk** → cmake [build/test]
- **mpdecimal** → no quoted formula dependency retained
- **mpg123** → no quoted formula dependency retained
- **mpv** → meson [build/test], ninja [build/test], pkgconf [build/test], ffmpeg [runtime], jpeg-turbo [runtime], libarchive [runtime], libass [runtime], libbluray [runtime], libplacebo [runtime], little-cms2 [runtime], luajit [runtime], mujs [runtime], rubberband [runtime], uchardet [runtime], vapoursynth [runtime], vulkan-loader [runtime], zimg [runtime], molten-vk [runtime] (macOS or architecture condition)
- **mujs** → no quoted formula dependency retained
- **nasm** → no quoted formula dependency retained
- **ninja** → no quoted formula dependency retained
- **openssl@4** → ca-certificates [no-linkage]
- **opus** → no quoted formula dependency retained
- **pcre2** → no quoted formula dependency retained
- **pixman** → meson [build/test], ninja [build/test]
- **pkgconf** → no quoted formula dependency retained
- **python-setuptools** → python@3.15 [build/test]
- **python@3.15** → pkgconf [build/test], ca-certificates [no-linkage], mpdecimal [runtime], openssl@4 [runtime], sqlite [runtime], xz [runtime], zstd [runtime]
- **readline** → no quoted formula dependency retained
- **rubberband** → meson [build/test], ninja [build/test], pkgconf [build/test], libsamplerate [runtime], libsndfile [runtime]
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
41. nasm
42. dav1d
43. fast_float
44. mpg123
45. lame
46. libvmaf
47. libvpx
48. opus
49. sdl3
50. sdl2-compat
51. svt-av1
52. x264
53. x265
54. ffmpeg
55. libogg
56. flac
57. help2man
58. fribidi
59. giflib
60. gobject-introspection
61. graphite2
62. icu4c@79
63. harfbuzz
64. jpeg-turbo
65. libb2
66. libarchive
67. libunibreak
68. libass
69. libudfread
70. libbluray
71. webp
72. libtiff
73. little-cms2
74. shaderc
75. vulkan-headers
76. vulkan-loader
77. libplacebo
78. libsamplerate
79. libvorbis
80. libsndfile
81. libtool
82. luajit
83. molten-vk
84. mujs
85. rubberband
86. uchardet
87. zimg
88. vapoursynth
89. mpv
