# Playback intent: bounded source contract proposal

The historical source review below is pinned to `06fdf279ef40d74b6b620e24cefc4e798d9041f9`; its line references and adapter behavior describe that revision. The current-source update is recorded separately below. Neither review is a production acceptance claim. The actual selected Jellyfin Web caller and its promise and retry contract remain to be examined before choosing observable cancellation behavior.

## Historical paths at 06fdf27

- `native/mpvVideoPlayer.js:248-259` awaits `createMediaElement(options)` before `setCurrentSrc`. The former returns resolved promises in both paths (`:518-586`). The latter creates a resolve-only promise and passes its resolver to `player.load` (`:309-365`). This resolves with the native call's return, not with proof that playback has started. Synchronous exceptions can still reject the promise.
- `src/player/PlayerComponent.cpp:272-276` implements `load` by calling `stop()`, queuing the replacement and returning `true`. `queueMedia` returns early when mpv is unavailable (`:280-285`). The returned value therefore does not by itself establish successful playback.
- Native end-file errors set a message (`src/player/PlayerComponent.cpp:498-515`); the error state emits only that message (`:467-469`, declaration in `src/player/PlayerComponent.h:206`). It carries no playback request or session identifier.
- The adapter's error handler calls `removeMediaDialog`, asks about transcoding, then emits a web error (`native/mpvVideoPlayer.js:210-236`). Both error cleanup (`:479-480`) and public `stop` (`:468-476`) call the same native `player.stop()`; `destroy` also calls cleanup (`:499-500`). Native `load` itself calls `stop`. Native `requestNativeStop` and web `stop` have separate entry points, but the current web stop call carries no origin (`src/player/PlayerComponent.cpp:776-789`).

## Current adapter and native commands at 59155af

The prompt guard in `native/playbackErrorPrompt.js:2-31` suppresses a pending prompt's stale error event after a newer intent or prompt. `native/mpvVideoPlayer.js:222-234`, `:443-450` and `:475-477` invalidate it synchronously for play, stop and destroy. This is prompt handling, not native request or stop-origin correlation.

Commit `93efdb3` made media-element setup synchronous: current `native/mpvVideoPlayer.js:222-234` calls `createMediaElement` and `setCurrentSrc` in the same turn, while retaining the asynchronous outer `play` promise and native load callback. Current `createMediaElement` is at `:495-562`. The ordering and prompt tests are `tests/test_mpvVideoPlayer_order.js` and `tests/test_mpvVideoPlayer.js`; the separate [native dispatch plan](synchronous-dispatch-native-plan.md) describes outstanding acceptance evidence. The native `load` success limitation described above remains relevant. These changes do not select a cancellation settlement policy or prove production recovery.

The native command definitions have moved since the historical review: `src/player/PlayerComponentCommands.cpp:8-16` implements load and `:19-50` implements stop and its checked command. `queueMedia` remains in `src/player/PlayerComponent.cpp:278-284`, including the early return when mpv is unavailable. The optional diagnostic observer records command activity; it does not add production request or stop-origin correlation.

## Proposed boundary for design

Associate each accepted playback request with a native correlation identity, and carry the relevant identity through fault and completion observations. Record the origin of stop or replacement intent so that internal fault cleanup or recovery can be distinguished from an explicit user stop and a replacement load. A genuine stop or replacement must invalidate any recovery or deferred intent tied to the prior request. The precise API, ordering, lifetime and race behavior require a separate design and validation; this proposal does not change the web adapter, suppress errors, or define a promise settlement policy.

In particular, do not assume that resolving a canceled deferred `play()` is harmless or that rejecting it avoids retry. Obtain the exact Jellyfin Web bundle/version used by the target server and inspect the actual callers of this player, including how they handle play resolution, rejection, cancellation, error events and transcode or other retries. Those callers are not present in the reviewed adapter source. Reconcile their behavior with stop, replacement and late fault ordering before selecting an externally observable contract.

### Candidate public web caller

On 2026-11-05, the public `https://jellyfin.curtpme.com/web/index.html` referenced build query `26148176bec435074cdd`. Its served `main.jellyfin.bundle.js` was 520,778 bytes with SHA-256 `5ddd1a9db7171efe85d499687015c9c9b3fe9cda5822b9e1ea83b3907a22477e`. The exact bytes were retained in ignored `deps/w553-web-contract/`. This endpoint is a candidate: the app's selected server and its served web bundle have not been established.

In that minified bundle, byte offset 129260 calls the selected player's `play(m)` and attaches success and failure handlers. Both perform shared initialization; the failure handler schedules the playback error handler with the stream information. At byte offset 138630, that handler can request another stream when transcoding is supported (the condition appears at offset 139012); otherwise it reports a playback error. A separate non-media path at offset 126410 catches a rejected play, stops the player and reports an error. These observations establish that, for this candidate caller, rejection can enter error or retry handling and resolution takes a success path. They do not determine which settlement is correct for a canceled deferred request, or establish behavior of a different server's bundle.

## Fixture evidence limits

`tests/c2_fault_restore_permission.h:8-60` is explicitly a fixture-only, single-writer permission fed by caller evidence. In `tests/c2_fault_restore_case.h:118-127`, `currentIdentity` starts from a captured identity and refreshes core, controller, binding and G1; the other captured fields do not establish current production playback intent. The synthetic restore fixture is useful evidence for its own scenario, not proof that production user stop, replacement, error or retry intent is correlated correctly.
