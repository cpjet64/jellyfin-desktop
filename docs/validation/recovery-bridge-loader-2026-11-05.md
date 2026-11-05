# Recovery bridge and loader validation — 2026-11-05

This record covers published source `5800e29dc81237b2ebcc5ea241421fc5c50c0365`. Related integrated commits are:

- Host session: `74f6134388f32716f4d1d7d918d252c685de38b2`.
- Web adapter: `5aedf1dcf9b3055b7f49e7ef8c9ef2876ff71843`.
- Producer design: `532a8e07f84fce61312637e92d2e2e1ed37d0069`.
- Windows loader: `ee25e8f9f5f561befc4522320496d3d0aae3f43a`.
- Tests and gates: `d17c7abf413656fdb36a2812e2576545792c6c66`.
- Loader failure trace: `2c8a6b251026d3db5ed56967895ecdfdc3fdfce8`.
- Normalized fixture input pin: `5800e29dc81237b2ebcc5ea241421fc5c50c0365`.

At `2c8a6b2`, the local Release suite passed 33 tests and the focused Debug suite passed 9. The first held-fixture compile stopped because the loader input pin used a raw-file hash where the copy generator checks normalized line endings. This was a fixture pin failure, not an observed loader failure. At `5800e29`, all 7 fixture-copy tests passed and the held fixture compiled. Its dependency audit covered 52 entries, including the copied loader and header, with no original player header selected; the fixture itself was not run.

Local offline checks at `2c8a6b2` passed 45 player/adapter and 4 input JavaScript tests, 5 mocked loader-driver tests, two routing suites of 12 each, 54 macOS protocol tests, and 40 Linux model and adapter tests. The subsequent pin correction did not alter production source. These are local checks, not native Linux or macOS playback evidence.

At `5800e29`, clean local Windows builds passed 36 tests for the static ON/ON configuration and 33 for the system OFF/OFF configuration (`USE_STATIC_MPVQT`/`MPVQT_EXTERNAL_EVENTS`). The system build used a newly rebuilt shared MpvQt dependency; neither ran the held fixture or application. The respective executable SHA-256 values were `8d9372739339b326c744d990376cc270e06b894889fe99fa4b309ac36876063a` and `dd1f75d13ce10a6ba422b21964e6503a5d4fbbb99a3ff71fcb8d4b02ee8ac2c4`.

At the same source revision, a compiled comparison helper verified that each configuration's actual compiled resource object contains byte-for-byte matches for all six current native scripts. Both helper compilations and comparisons exited successfully. This establishes resource-object contents, not package contents or a live page.

One published instrumented Windows loader run at `5800e29` passed its protected-file, hash, absent, incompatible, dependency and PATH cases. In the corrupt input case, the actual loader error was 193 and the policy result was 126; the thread error mode changed 2 → 3 → 2 while process mode stayed 0. The cause of the earlier W726 timeout remains unconfirmed; this run makes no GUI claim.

The bridge identifies a host-created view/channel session, not a document. The initial `LoadStarted` currently revokes or blocks the normal handshake, so usability on initial navigation remains unresolved. Capability remains unsupported and the candidate is null. The explicit-context validator does not establish an authenticated account source. The producer patch is a design only; no fork or producer qualification is established.

Fresh resume, restart, tickets, native WebEngine and GUI playback have not been demonstrated. CI has not been run for this evidence, and the separate CI allocation remains held rather than transferred. This record does not establish end-to-end recovery.
