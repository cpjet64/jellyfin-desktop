# Corrected C2a diagnostics: Windows failure checkpoint

The corrected render-failure and active-retirement cases were each run once
in separately contained Debug and Release processes at source revision
`8cff80c61420ca708fc3050e4d2e910ec6405470`. Both configurations built
successfully and passed the named `test_c2a_hold_gate` CTest before the native
cases. **All four native cases failed.** This checkpoint does not establish
playback continuity, complete C2a, C2b, G2 creation, production recovery, or
Linux or macOS behavior. The earlier matrix remains unchanged.

| Configuration | Fixture executable SHA-256 | Guard CTest log SHA-256 |
| --- | --- | --- |
| Debug | `60fc8537028055fabadcfbd53c13584474f0c7765316a5919b40f6281058debb` | `08c8401daea96c30c83149cd1d612d62197ac209060055909bbb0020c8350af5` |
| Release | `3bddf8762511e76c5655a787fabb763c52c8c0b133d6bc4441a713ff842d2045` | `e9ff144ecebe64b0997105cb4973a9119637a67b6ebe05b94c90a0bf828560bf` |

Each case recorded application exit 1 and helper exit 7, with no QTest footer.
The helper reported a known zero job count before and after cleanup, and no
termination request. A subsequent exact-path process check found no remaining
task process. These observations do not turn the failed tests into passes.

Both render-failure runs reached the strict diagnostic assertion after
receiving a stuck-render message followed by no-render-context and VO-window
initialization critical messages. Their records include the injected render
failure, the matching retirement ACK, and a later end-file event with reason 4
and error -15. No causal conclusion follows solely from message ordering.

The active-retirement records now show the selected real callback held at
unregistration entry, independent release, one free and matching generation-1
GUI ACK, followed by rejection of a retained stale update. The records also
show one error end-file event, reason 4/error -15, idle true and zero canceled
signals before cleanup. Both cases then failed the terminal Stop check. The
available case record does not establish whether the Stop command itself was
rejected; failure of the terminal check is a separate observation.

The following deltas are milliseconds calculated from the retained monotonic
timestamps. “Queued” is the diagnostic publication for GUI delivery;
“received” is the GUI handler receipt. The free and ACK fields are sampled
independently from atomics, as recorded in the JSON, not a single synchronized
snapshot. The post-handler observation is taken after the waiting fixture
regains control, not at the handler's exact return. These deltas do not
establish why the warning occurred.

| Measured interval | Debug | Release |
| --- | ---: | ---: |
| Render failure queued to GUI receipt | 10.8938 | 7.3920 |
| Render failure GUI receipt to post-handler observation | 30.0117 | 41.5075 |
| Render failure free entry to return | 186.9390 | 202.3018 |
| Render failure GUI receipt to matching ACK receipt | 195.1664 | 217.2013 |
| Active retirement request to unregister entry | 11.8784 | 11.5142 |
| Active retirement free entry to return | 214.8708 | 217.4319 |
| Active retirement request to GUI ACK receipt | 230.2925 | 238.6784 |

The byte-identical measured JSON files in the adjacent
[`c2a-corrected-2026-11-04`](c2a-corrected-2026-11-04/) directory retain render
timing and phase observations, active worker/pre-assert/GUI/observed snapshots,
and the runner's actual application/helper outcomes. The files include
process-local diagnostic identity values but no private file paths. Original
QTest and helper logs remain local because they contain machine-specific paths;
their hashes below identify the actual unredacted bytes.

| Case | QTest SHA-256 | Helper stdout SHA-256 | Helper stderr SHA-256 |
| --- | --- | --- | --- |
| Debug render | `4250c43f2dbc5f2fadd7bcdfc356ceb1ed6d2f62341a666f7ec4a86f5a04f8e4` | `1ce784c10c34d12c3c75a2f08e69bf467229a840113b9da091541b5b1b2e78ba` | `be99db08f266baac5f6ee8f5532f0b22305e14a6754b764d2ca9bdb3ce7b160c` |
| Debug active | `9487f0292605bcf827af2232c5ad5bf6e55148fa86419d2eed6279fc2822fd52` | `5faeeae935dcb65de01954e416b39636c74262483cadccb9a69c23db7a29d4cf` | `eb5b580450434e9d2a5f57073a7e327a062691131c1d78156f915e3209e91f99` |
| Release render | `bbf649219f749f22bd0e8b429d083b0ce5d75d169d6a3e3cc396f592396e1a13` | `b331aa52a65a337b7b8e72a9e5c9ad3a193db67714fbf22cde3f4a8137ff4f7b` | `521b7c5638c76044166298b459efc82662f9056ea93febc1810b3d5861c5e5c4` |
| Release active | `ef26fff6755a9c5ed0a1e83cea3254fca23c40daedf7229b1dbd1bafb9a9fcab` | `13d4a6c28f284ff7dc8638b91f005d7ed083c7a2ecc8b0ed9d74ddabaa040dce` | `e9e6e8715f56d2c699e876f02ade3968bc4335a7c304263211c8962a3df89a6c` |
