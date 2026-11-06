# Recovery opening and CI correction evidence — 2026-11-05

This record covers source `3e3028b3980b124114575756277bce1b9d70dd07`.
Pipeline 32684 failed on the earlier baseline; this record does not replace
that pipeline or assert a result for a later revision.

The integrated corrections are `807348e` (portable proof root-policy test),
`6588404` (canonical LF selector pins), `9125683` (portable gate registration),
`7c512dd` (runtime configure fixture policy), and `3e3028b` (provisional host
opening). The three LF pin refreshes reflect line-ending normalization, not a
source-content change. Historical evidence remains preserved.

At `9125683`, the focused integration checks passed: root policy 9, portable
assembly 3, selector pin 7, launcher 10, native ordering 12, and the Windows
smoke gate. These results precede the opening commit.

The retained configure-only comparison in `deps/w774-proof/` used CMake 4.3.2,
Ninja 1.12.1 and MSVC 19.44.35229. Both CMP0112 OLD cases failed and both NEW
cases passed, with and without the application target. The generated inputs,
raw outputs and exits are in `old-app-0/`, `old-app-1/`, `new-app-0/` and
`new-app-1/`; `before.txt`, `after.txt` and `compare.py` record tool identities
and procedure. CI used CMake 4.2.3-msvc3, which was not reproduced locally.
W773's summary in `deps/w773-proof/results.txt` retains its earlier comparison
but not its deleted temporary inputs and raw outputs; W774 is a separate,
retained comparison that addresses that evidence gap.

At `3e3028b`, local Windows static ON/ON and system OFF/OFF builds passed 36/36
and 33/33 headless tests respectively, including the actual bridge host and
session tests. Logs are in `deps/w775-proof/`; the first sandbox build failed
with MSVC C1902 and the same build and tests passed on the narrowly authorized
host retry. The resulting static and system application SHA-256 values are
`8b58ed6eb71ad6d95ad60c0ac8e57ac5dbf6641d628b8f074e20255304d7e80c` and
`e23ccaddcf639443f554d24305b11f790eda85df295ad3aa61a5cb13755d9cdb`.

The opening handshake is provisional view/channel evidence only. Qt events do
not carry the host opening marker, so a delayed same-URL event cannot establish
its origin or a document identity. Capability remains unsupported and the
adapter candidate remains null; no native recovery authority is granted. The
separate private-consumer audit found no direct references within its inspected
scope. The producer artifact remains a draft and is not included here.

No native WebEngine, playback or final user acceptance was performed for this
opening. No CI result is claimed for `3e3028b` or this evidence update.
