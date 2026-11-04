# Tahoe media-library remote patch identities

These are literal patch declarations from the pinned Homebrew core formulae
for the library candidate graph. They are metadata, not locally acquired or
applied patch bytes. Verify each exact SHA-256 before use; local and inline
patches are separately indexed. Conditional applicability and the complete
build manifest still require review.

| Component | Patch URL | SHA-256 |
| --- | --- | --- |
| mpv | https://github.com/mpv-player/mpv/commit/75b2ccfeb1ce4ed5a40ac9860fa74f3d1265e13f.patch?full_index=1 | `3906b98b02071a0d5747a400406494ca69cef7afd8d3eee4a99fdbe40dc90c1f` |
| mpv | https://github.com/mpv-player/mpv/commit/c5d391adba7bd024954d0df1e0405f5749f4d4ca.patch?full_index=1 | `769b218df220738cc1cf9f81cf696c16518c5dfe56a5ef028e33b22536e0e924` |
| mujs | https://github.com/ccxvii/mujs/commit/e21c6bfdce374e19800f2455f45828a90fce39da.patch?full_index=1 | `e10de8b9c3a62ffe121b61fe60b67ba8faa68eaace9a3b17a13f46a2cc795a11` |
| libsamplerate | https://github.com/libsndfile/libsamplerate/commit/e4a0ab46887029e0f65f145ba3987cc592f18200.patch?full_index=1 | `0826fb59d733188645f3dc207c938580970700381b9bc87058b137723cb30bba` |
| libsamplerate | https://github.com/libsndfile/libsamplerate/commit/1abc639420b2df8b9ff2e0bdcc28cf6613c7c0d0.patch?full_index=1 | `b6fb61c763a0f072ef2ebb3a311037d2429a5e294141f6d5e552ccd25efabdc0` |
| readline | https://ftpmirror.gnu.org/readline/readline-8.3-patches/readline83-001 | `21f0a03106dbe697337cd25c70eb0edbaa2bdb6d595b45f83285cdd35bac84de` |
| readline | https://ftpmirror.gnu.org/readline/readline-8.3-patches/readline83-002 | `e27364396ba9f6debf7cbaaf1a669e2b2854241ae07f7eca74ca8a8ba0c97472` |
| readline | https://ftpmirror.gnu.org/readline/readline-8.3-patches/readline83-003 | `72dee13601ce38f6746eb15239999a7c56f8e1ff5eb1ec8153a1f213e4acdb29` |
| readline | https://ftpmirror.gnu.org/readline/readline-8.3-patches/readline83-004 | `1f189c4566aa35950964647b9ec3be1a821e42b86048129c63eb7f9d4d2f5a74` |
| readline | https://ftpmirror.gnu.org/readline/readline-8.3-patches/readline83-005 | `5481adbe161c9f6ba972c74db9e18c7aa351d3df3035cc4a673a1aa06247d1c6` |
| readline | https://ftpmirror.gnu.org/readline/readline-8.3-patches/readline83-006 | `4ebd261a608287796e171af0ea9af5a1e3122e46672accda2484b6698c47c1a4` |

The Readline patches apply at strip level zero, in ascending order, to the
8.3 source archive. Their URLs are expanded from the formula's fixed numbered
list; they are not discovered from a live directory at build time.
