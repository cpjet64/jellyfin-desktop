# Healthy mpv thread fixture regression: 2026-11-04

The Windows debug and release runs tested source revision
`3867132e239c57c4a1bb0d866f3a3348cb6aa471`. Each ran the healthy playback
fixture on a hidden desktop using generated media. Both QTest reports recorded
3 passed, 0 failed, and 0 skipped. Each helper recorded a known natural exit
code of 0, zero active job processes before and after cleanup, no termination
request, and empty standard error.

During the held GUI interval, the fixture's `MpvRenderer` subclass recorded
22 render entries and returns in each run. Two captured frames had increasing
sequence numbers within that interval, differed in content, and showed the
generated moving-square pattern. Recorded playback positions advanced from
566 to 900 ms. Each lifecycle record reports a successful Stop, idle state,
one render-context free and acknowledgement, followed by item and core teardown;
the render worker joined and the context and surface were destroyed.

| Evidence | Debug SHA-256 | Release SHA-256 |
| --- | --- | --- |
| Fixture executable | `c07439e4f3ca3e329d44e3461f43a19693271b3588c22d653cc8075c7bae8ff3` | `00bebd754ce17427344a95eaeb19d2850dbf4dceba2cf2993aa1a2bd27e9ff98` |
| QTest report | `8d8115d55eb260d23a1bb0b73d63c14d4f8cd4b3834d7259148cdf5a2b626a59` | `ecd3238856f9736532b27b9df5bed5fdb3ffe8a11714639e6137e4dea528717c` |
| Hold trace | `ad2ce3ef6578f829336cebd8b8cdbed709db01efed8f87ee0b1af1fd6447a377` | `1b07fed458266e728d3bdb6bd2a8c29f7167e480e61e4abb18b4216e6ac3c944` |
| Lifecycle trace | `00edec6940b8edcf03d94bcbc83bd53065403f48220075b7f820da0fbc0516f9` | `c89b7264c028d851b6a7d064b609faefdff193ef3745c0e68022fe974e233714` |

The first frame in both runs has SHA-256
`65bcb0d2ba982d5a37689ce8242524a2c3326a3cab6b9d8286cb3378232183ab`;
the second has SHA-256
`b9816773e9ee4bd68b60a4c7148660c7d7cb0f7434dd68f1b4aa9e546a197cd1`.

This is healthy fixture regression evidence. It does not establish C2a
retirement acceptance, G2 reconstruction, actual lost-context recovery, or
native Linux or macOS acceptance. The earlier thread-startup observations
remain recorded separately in [the prior report](mpv-thread-startup-2026-11-04.md).
