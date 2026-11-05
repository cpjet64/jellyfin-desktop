# Playback intent: bounded source contract proposal

This is a source review of `06fdf279ef40d74b6b620e24cefc4e798d9041f9`, not an implementation or acceptance claim. It covers the native video adapter's request and error paths. The actual Jellyfin Web caller and its promise and retry contract remain to be examined before choosing observable cancellation behavior.

## Current paths

- `native/mpvVideoPlayer.js:248-259` awaits `createMediaElement(options)` before `setCurrentSrc`. The former returns resolved promises in both paths (`:518-586`). The latter creates a resolve-only promise and passes its resolver to `player.load` (`:309-365`). This resolves with the native call's return, not with proof that playback has started. Synchronous exceptions can still reject the promise.
- `src/player/PlayerComponent.cpp:272-276` implements `load` by calling `stop()`, queuing the replacement and returning `true`. `queueMedia` returns early when mpv is unavailable (`:280-285`). The returned value therefore does not by itself establish successful playback.
- Native end-file errors set a message (`src/player/PlayerComponent.cpp:498-515`); the error state emits only that message (`:467-469`, declaration in `src/player/PlayerComponent.h:206`). It carries no playback request or session identifier.
- The adapter's error handler calls `removeMediaDialog`, asks about transcoding, then emits a web error (`native/mpvVideoPlayer.js:210-236`). Both error cleanup (`:479-480`) and public `stop` (`:468-476`) call the same native `player.stop()`; `destroy` also calls cleanup (`:499-500`). Native `load` itself calls `stop`. Native `requestNativeStop` and web `stop` have separate entry points, but the current web stop call carries no origin (`src/player/PlayerComponent.cpp:776-789`).

## Proposed boundary for design

Associate each accepted playback request with a native correlation identity, and carry the relevant identity through fault and completion observations. Record the origin of stop or replacement intent so that internal fault cleanup or recovery can be distinguished from an explicit user stop and a replacement load. A genuine stop or replacement must invalidate any recovery or deferred intent tied to the prior request. The precise API, ordering, lifetime and race behavior require a separate design and validation; this proposal does not change the web adapter, suppress errors, or define a promise settlement policy.

In particular, do not assume that resolving a canceled deferred `play()` is harmless or that rejecting it avoids retry. Obtain the exact Jellyfin Web bundle/version used by the target server and inspect the actual callers of this player, including how they handle play resolution, rejection, cancellation, error events and transcode or other retries. Those callers are not present in the reviewed adapter source. Reconcile their behavior with stop, replacement and late fault ordering before selecting an externally observable contract.

## Fixture evidence limits

`tests/c2_fault_restore_permission.h:8-60` is explicitly a fixture-only, single-writer permission fed by caller evidence. In `tests/c2_fault_restore_case.h:118-127`, `currentIdentity` starts from a captured identity and refreshes core, controller, binding and G1; the other captured fields do not establish current production playback intent. The synthetic restore fixture is useful evidence for its own scenario, not proof that production user stop, replacement, error or retry intent is correlated correctly.
