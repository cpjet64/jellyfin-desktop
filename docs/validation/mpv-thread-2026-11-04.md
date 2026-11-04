# Windows mpv render-thread experiment — 2026-11-04

This is a single diagnostic experiment using a generated local clip, the
reviewed threaded Qt scene, an actual `MpvVideoItem` and `PlayerComponent`, and
the bundled diagnostic adapter. It is not a production recovery test.

## Debug run

The Windows Debug test used source commit
`3ff7f6ca5ebd380b70415adea556bc7f18c6b48e`, Qt 6.9.3 shared Debug with MSVC
2022, and the configured bundled mpv `20261018-git-01e2533f31`. The executable
SHA-256 was `81D2AFB59DE70FEF274A111B6E150F8EEEF22CACAED4EB1860E1506E5320E410`.
QtTest reported **3 passed, 0 failed, 0 skipped**; the containing process exited
normally with code zero and no forced cleanup or remaining job processes.

Before withholding GUI events, the fixture observed changing nonblank frames
and advancing playback position. During a **353,604,000 ns** interval without
GUI event pumping or synchronization, it recorded 17 synchronous GUI position
getter entry/return pairs, advancing from 566 ms to 900 ms, and 23 paired render
wrapper entries and returns on the original worker context. Two distinct
nonblank frames captured within that interval are included:

- [Frame 1](mpv-thread-2026-11-04/debug/mpv-hold-1.png): SHA-256
  `65BCB0D2BA982D5A37689CE8242524A2C3326A3CAB6B9D8286CB3378232183AB`.
- [Frame 2](mpv-thread-2026-11-04/debug/mpv-hold-2.png): SHA-256
  `B9816773E9EE4BD68B60A4C7148660C7D7CB0F7434DD68F1B4AA9E546A197CD1`.

[Hold observations](mpv-thread-2026-11-04/debug/mpv-hold.json) record the
monotonic interval, frame sequences, getter pairs and render-wrapper pairs.
The fixture subclass calls the actual base `MpvRenderer::render()` and requests
another frame itself. These are wrapper observations, not a raw libmpv render
call count or proof of progress without fixture-driven redraw.

The native Stop command was accepted; idle was subsequently confirmed. The
fixture then observed one G1 free and one matching retirement acknowledgement,
with the original controller, core and binding retained through acknowledgement.
The binding was released during item teardown, and the Qt worker joined.
[Lifecycle observations](mpv-thread-2026-11-04/debug/mpv-lifecycle.json)
record these events and the cleanup sequence. The mpv and Qt cleanup order
numbers are separate counter domains and must not be compared with each other.

The Debug log also contained initial `vo/libmpv: No render context set` and
`Error opening/initializing the VO window` diagnostics before frame playback.
The test's Player error and deadlock-warning checks did not fail, but these
startup diagnostics are retained as a limitation requiring interpretation;
the result is not a claim that the log was free of critical messages.

## Release failure

The Release attempt used the same source and shared Qt 6.9.3 Release with MSVC
2022. Its executable SHA-256 was
`55162858288F9431150B98A1B191A91ECB4D6DC357D1F609269416A18249076C`.
It **failed**: QtTest reported 2 passed, 1 failed, 0 skipped. It exited with
code 1; the helper marked forced cleanup true and reported zero active job
processes. This is not an accepted Release result.

During a 351,347,800 ns GUI hold, the fixture recorded 17 getter pairs, position
advancing from 533 ms to 866 ms, and 22 render-wrapper pairs. Its saved frames
and structured observations are included for diagnosis:

- [Frame 1](mpv-thread-2026-11-04/release/mpv-hold-1.png): SHA-256
  `D4EE81B515A102EF906CFEE0691A2E2CA803E9B1F87D1BCDA089BE2549F5D4C6`.
- [Frame 2](mpv-thread-2026-11-04/release/mpv-hold-2.png): SHA-256
  `65BCB0D2BA982D5A37689CE8242524A2C3326A3CAB6B9D8286CB3378232183AB`.
- [Hold observations](mpv-thread-2026-11-04/release/mpv-hold.json) and
  [lifecycle observations](mpv-thread-2026-11-04/release/mpv-lifecycle.json).

After Stop was accepted and idle was confirmed, the log recorded the warning
`mpv_render_context_render() not being called or stuck`. The final QtTest
warning gate failed at `tests/test_mpv_thread_independence.cpp:135`. The
lifecycle record nevertheless reports one free, one matching G1 acknowledgement,
binding release, worker join and resource cleanup. Those observations do not
turn the diagnostic failure into a pass. The cleanup and mpv order counters
remain separate domains.

## Scope

The test performed one healthy local load. It did not create a G2 render
generation, replay media, inject real GPU loss, or prove recovery. Authenticated
server playback is not covered. Native Linux and macOS execution remains
outstanding; no cross-platform pass is claimed.
