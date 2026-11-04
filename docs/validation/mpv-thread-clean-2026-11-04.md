# Windows mpv threaded diagnostic — 2026-11-04

The controlled and healthy runs used [commit `cd46048714421afb0e4c23a2226e4730c1d6cf14`](https://gitlab.curtpme.com/cpjet64/jellyfin-desktop/-/commit/cd46048714421afb0e4c23a2226e4730c1d6cf14), Qt 6.9.3 and bundled diagnostic MpvQt with external events. Debug and Release builds used the diagnostic-only target; no application or package was built. The hidden-desktop helper SHA-256 was `706e0a939b4dff4b2f2996996863943c3d3a5dfba6dc46e96578f4fb73bd7a42`.

| Input | SHA-256 |
| --- | --- |
| Debug fixture | `67a4317e1ee24f72effdfbe89af50bc2f9d548b4b564c6055f9d3b6fb1a62b61` |
| Release fixture | `c5bae8d0f77f664a0a1a95fc64904bcbaef7f9cc8e95b0c6033f79919077d88a` |
| Generated clip | `be9dc89757c53be878b9bcfb399a6da3146d7b39288323b68941a6689f878ff1` |
| libmpv DLL | `5c5c9d094859bdc60b8b27b2d832db8fa8c2e487efbd35f5aea5afe6960dad39` |

Both healthy cases reported QTest **3 passed, 0 failed, 0 skipped**, complete footers and natural process exit 0. The helper reported no termination request and zero active job processes before and after cleanup. The real album-art cache was inside each owned case directory; no critical or stuck-render diagnostic was recorded.

During the GUI hold, Debug recorded 17 position getter pairs advancing from 566 to 900 ms and 22 render-wrapper pairs over 355,132,700 ns. Release recorded the same counts and positions over 352,424,800 ns. Each saved two distinct nonblank frames: Debug sequences 58 and 60; Release sequences 57 and 59. The fixture explicitly requested redraws; wrapper entry and return observations are not proof of an independent production redraw mechanism.

| Frame | SHA-256 |
| --- | --- |
| Debug and Release frame 1 | `65bcb0d2ba982d5a37689ce8242524a2c3326a3cab6b9d8286cb3378232183ab` |
| Debug and Release frame 2 | `b9816773e9ee4bd68b60a4c7148660c7d7cb0f7434dd68f1b4aa9e546a197cd1` |

The checked properties showed a configured `libmpv` VO and `force-window=yes` before Stop. In both cases Stop was accepted, `force-window=no` was set successfully, one cancellation arrived, and the terminal checks observed idle with `current-vo` unavailable before retirement. Each recorded one free and one acknowledgement, released the player binding, joined the worker and destroyed the context and surface in the checked order. Property timestamps are receipt times, not claims about exact core transition times. The [Debug](mpv-thread-clean-2026-11-04/debug/mpv-lifecycle.json) and [Release](mpv-thread-clean-2026-11-04/release/mpv-lifecycle.json) records and hold JSON and images are included alongside this report.

The controlled Debug case intentionally failed before media submission: QTest reported **2 passed, 1 expected failed, 0 skipped**, and the no-load verifier passed. The [unaltered lifecycle record](mpv-thread-clean-2026-11-04/controlled/mpv-lifecycle.json) records no submission or Stop, one free and acknowledgement, and settled resources. The original UTF-16 console SHA-256 was `361d862e2c93e74860a8eafc30841fdda688937b36a808e7d8660b56d54c363d`; a derived UTF-8 copy used by the verifier was `88c03f2c050a68ae13afb5b417ed3e0a992317238c4894aa7c43c86102ee14cb`. Conversion round-tripped to the original bytes, including BOM and line endings; the case was not rerun.

An earlier fresh build at `5dd249a` failed because Qt moc generated no metadata for a custom raw-string expression. An equivalent escaped string corrected that compile failure before these runs. This diagnostic does not prove production recovery, G2 recreation, C2a recovery, or Linux or macOS behavior.
