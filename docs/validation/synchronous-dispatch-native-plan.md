# Synchronous dispatch: native diagnostic plan

## Diagnostic gate registration plan — 2026-11-05

The existing production builds do not enable the
`PLAYER_INTENT_DIAGNOSTIC_ONLY` observation seam. The option is to default OFF;
its diagnostic configuration excludes application, installation and packaging
targets. It must not enable C2 or change the selected MpvQt or external-event
mode. The existing render-thread holds remain in force. A separate diagnostic
build and an individually selected WebEngine fixture case are needed; the
existing full-fixture scenario loops are not proof that this case ran.

Register the single Stop case as a required, separately named diagnostic
obligation for each of Windows, Linux and macOS in both Debug and Release.
Each platform route must build only the diagnostic fixture and its required
dependencies, use the existing platform-specific owned desktop and process
containment, select the exact case, impose a finite timeout, and require its
named QTest result and successful cleanup. Missing, skipped, failed, timed-out
or uncertain results remain non-passing. Preserve the 28 Node checks and
existing gates; Node and mock tests cannot substitute for the six native
receipts. The source draft selects `RECOVERY_SCENARIO=synchronous-stop` in
`test_webclient_containment`, with `RECOVERY_MEDIA`, `RECOVERY_MEDIA_B`,
`RECOVERY_EVIDENCE_DIR`, `JELLYFIN_TEST_OWNED_ROOT` and
`JELLYFIN_TEST_SOURCE_SHA`. It is explicitly held before native execution;
the evidence schema and safe renderer route require review before wiring jobs.
This plan neither changes the default build nor authorizes execution, resource
allocation or a CI unhold.

This is a source-only plan. A fixture draft exists but remains held and has not
been run; no native playback result is claimed. Its purpose is to exercise the real web player,
QWebChannel and PlayerComponent/mpv path when Play is immediately followed by
Stop or a replacement Play. It does not propose a new intent API, origin API,
or cancellation policy. The synchronous JavaScript dispatch change is landed;
native execution requires separate safety review and authorization.

## Existing pieces and the remaining seam

`tests/test_mpvVideoPlayer_order.js` exercises the synchronous helper and adjacent
calls with a Node mock; it cannot establish
native receipt or actual playback. `tests/test_webclient_containment.cpp` and
`tests/webclient_fault_fixture.qml` already attach a real `PlayerComponent` to
QWebChannel in an off-the-record WebEngine profile and exercise a local clip
through an `MpvVideoItem`. They expose the player proxy directly, but do not
load or instantiate the actual web player class. `PlayerComponent::load()`
calls `stop()` then `queueMedia()`; the latter sends mpv `loadfile append-play`.
`PlayerComponent::stop()` returns void, although `checkedStop()` obtains the
actual `mpv_command` result internally. A JavaScript return, load callback,
queued command, or subsequent idle observation alone cannot prove that the
particular Stop command was accepted.

The narrow candidate is a new scenario in the existing WebEngine containment
fixture, using its existing QWebChannel player object and loading the actual
`native/playbackErrorPrompt.js` and `native/mpvVideoPlayer.js` into the test
page. Supply only the minimal test-owned dependencies used by that class, as
in the Node test. Use the actual synchronous helper and dispatch adjacent calls
without awaiting UI preparation. A native callback may be delayed as an
observation control; releasing it must not dispatch further commands. Do not
replace the QWebChannel player with a mock. Before implementation, verify the
landed web-player API and establish a
test-only observation seam for ordered native `load`/`stop` entry and the
actual `checkedStop` result. The current fixture exposes neither that result
nor ordered PlayerComponent call receipts. A callback/spy confined to a
diagnostic test build around the existing command return is a candidate; it
must observe the production command exactly once and cannot issue a second
Stop, alter its arguments, change production behavior, or expose a new product
API. Its placement and lifetime need independent review before use.

## Scenarios and evidence

Use separately named test-owned local `.y4m` files A, B and C produced by
`tests/generate_recovery_media.py`; the generator is deterministic, so names
and hashes alone do not make the video content distinct. Bind source identity
to mpv's observed path and playlist entry, not frame appearance. Set finite
deadlines for each transition, record an ordered monotonic trace at the
JavaScript call, native method entry, native command result and mpv event
boundaries, and fail on a missing, ambiguous, or late result. A separate
`C2EventObserver` client can capture START_FILE, FILE_LOADED and END_FILE
without consuming the PlayerComponent client's events. Keep event offsets and
entry identities per scenario; do not infer native ordering from a JS promise.

1. Establish clip A is actually playing: observed A path and playlist entry,
   advancing player position, and two distinct visible frames from the real
   output. Invoke Play B and Stop adjacently through the real web player and
   bridge, without awaiting a callback. Require native load B receipt before
   the native Stop receipt, then the specific Stop's nonnegative mpv command
   result and terminal idle. Drain bridge and mpv events to a bounded
   observation boundary; no additional stale B load may follow Stop, and idle
   must remain stable with an empty/nonplaying playlist. If B reached playback,
   bind any matching end event to its observed entry; do not require it to have
   started before the adjacent Stop. If a native callback is deliberately
   delayed, release it and verify it adds no command. Retain trace and failure
   evidence; a finite observation proves only that bounded interval.
2. Start with A playing and invoke Play B followed immediately by Play C, again
   without awaiting the earlier callback. Require actual native load B then
   load C receipts in that order. Verify C is the current observed path/entry
   and actually advances with changing visible frames; a resolved Promise,
   `load()` returning true, or playlist queue membership is not a pass. Drain
   events and any deliberately delayed callback and require no later stale B
   load. Identify C by observed source and entry even if generated files have
   identical pixels.

Each scenario runs in a fresh process with separately created profile, media
and evidence directories. Reuse the bootstrap checks and exact owned-root
containment, new-only evidence writes, held process/runner lease, bounded test
timeout, and fixture cleanup/settlement obligations of the existing desktop
runner. Tear down observers before the mpv core and prove the launched process
and its children have exited; retain failure traces, command return codes and
partial evidence. Do not treat a timeout, renderer startup failure, unobserved
command result or uncertain teardown as success.

## Platform and proof boundary

The existing Windows GUI runner generates local media and uses the hidden
desktop helper; Linux and macOS have their own GUI paths in `dev/ci/native.py`.
Any implementation must add the scenario to each supported platform's actual
desktop gate with equivalent evidence and an independently checked result;
portable Node tests and mocked headless gates continue on Windows, Linux and
macOS but do not substitute for native WebEngine/mpv execution. The repository
currently holds certain render fixtures due to a render-thread contract issue;
do not bypass those holds or assume an existing fixture is safe merely because
it builds. Establish a safe route and applicable platform resources separately.
Record exact source revision, build identity, platform/session, generated media
hashes, scenario trace, exit/settlement and any skipped or blocked native case.
