# Request-specific preparation failure: source proposal

**Decision requested from Pro; not implemented or validated.** Provide a
non-destructive, request-specific terminal notification for a video load whose
native preparation fails. Preserve the existing `bool load(...)` and `void
queueMedia(...)` interfaces and their current admission semantics. The web
correlation value below is observational only: it is not a producer identity,
native authority, a recovery key, or permission to issue a command.

## Observed boundary

At `72f00ef4aef93afe486d177526dfb019d6332aa1`, `PlayerComponent::load` returns
true after initiating `queueMedia`; it does not prove preparation or playback.
`failOrdinaryPreparation` can emit `loadPreparationFailed(quint64, QString)`
from an immediate failure or a later retry/timeout. No web subscriber exists.
Do not use a JavaScript number to correlate a native 64-bit identity; use an
opaque, canonical decimal `QString` generated natively if that identity crosses
WebChannel. The existing web `_PlaybackIntent` tickets are local ordering aids,
not native producer identities. Do not derive association from URL, metadata,
an unqualified global error, or logged request content.

The current video `onError` delegates to `PlaybackErrorPrompt.handle`, which
calls `removeMediaDialog()`, invokes native `stop()`, and later emits a global
playback error. That path must not handle a preparation notification: an older
or queued failure could otherwise stop unrelated current playback. The audio
plugin already accepts `toast` in its constructor. The video plugin currently
accepts `confirm` but not `toast`; add the same dependency explicitly and use a
localized, fixed message such as “Could not prepare this item for playback.”
The displayed message must not contain the URL, token, native request ID, or
raw native error. No stop, dialog removal, retry, transcode prompt, or global
playback error accompanies this status.

## Proposed backward-compatible observation API

Add one distinct Q_INVOKABLE observed-load entry point with the existing load
arguments plus a bounded opaque web correlation string; its bool callback keeps
the existing early-admission meaning. It calls the *same* native load path,
without changing native request allocation, recovery checks, or the existing
method. Add a dedicated status-only signal containing that correlation string,
the native request as a decimal string, and a closed failure-reason enum/string.
Do not change or remove the existing `loadPreparationFailed` signal. Reject
malformed, oversized, duplicate, or absent correlation strings at this new
entry point without changing playback; never interpret them as authorization.

Associate the token inside native control-thread state with the exact native
request created by this invocation, at the allocation point and **before** any
preparation operation can fail or emit. The bounded mapping must include a
native bridge/session lifetime and request, be invalidated on replacement,
Stop, binding loss, teardown and counter exhaustion, and emit at most one
terminal status. Preserve the original request through permitted pause/track
revisions; do not move the 1.5-second deadline. Internal native event handling
and qualification must not depend on this mapping. Do not install a single
mutable “last token” around `load`: reentrancy or a nested load can associate
the wrong request. The wrapper must enter a scoped per-call association before
request creation and unwind safely on every early return and nested call;
proof of exact association and revocation is required before implementation is
accepted. A `false` early admission completes the local observation without
waiting for a later failure signal. A `true` result is only admission.

Use a fresh per-WebChannel-lifetime nonce plus a monotonically increasing local
counter encoded as a string. Register the pending token **before** invoking
the observed method, because failure can be emitted before its asynchronous
WebChannel callback. Fail closed if secure nonce generation or the counter is
unavailable; never reuse a token. A single helper owns the signal connection,
captured player object, bridge-lifetime nonce and bounded pending state. Match
only its exact pending token and current local play ticket, settle once, and
ignore stale signals and callbacks after replacement, stop, destroy, bridge
replacement or disconnection. Recheck state immediately before showing the
toast. Disconnect from the exact captured old player object, not a newly
resolved `window.api.player`; invalidate synchronously before disconnect.
The decimal native request is diagnostic association only and must not be
converted to `Number`, rendered, or used to authorize actions.

`queueMedia` remains a separate public native API and is not called by the
current video plugin. Its existing failure signal must never be routed through
the destructive prompt. A later caller that needs a request-specific queued
notification needs its own observed-queue association and lifecycle policy;
do not infer one from an untagged signal. A queued failure must leave an
unrelated playing item and its dialog untouched. This proposal does not claim
that existing unobserved queue callers receive a new visible message.

## Actual integration points

`native/nativeshell.js` exposes the bundled `_mpvVideoPlayer` class through
its plugin factory; it does not instantiate the player. The plugin dependency
injection supplies constructor dependencies, as already demonstrated by the
audio plugin's `toast`. Confirm the actual host injection contract and fallback
behavior when `toast` is absent before accepting the change. `createApi()`
returns QWebChannel objects and `window.apiPromise` installs `window.api`;
`ComponentManager` registers the native player object. No independent trusted
producer bridge is established by this status path.

Extract connection ownership and request/status handling into a cohesive new
`native/` module rather than expanding the existing 884-line video player.
Move the relevant connection-management code into that module so the oversized
file does not grow; do not weaken its existing signal handling. Native files
are collected by `src/CMakeLists.txt` and aliased under
`/web-client/extension` by `CMakeModules/GenerateQtResources.cmake`. Add the
new resource path before `mpvVideoPlayer.js` in
`src/system/NativeScriptPaths.h` and the corresponding ordered script lists in
the Node and synchronous-page harnesses. Check all packaging and injection
paths rather than assuming an added file executes. New files and functions
must meet 500/75 limits; extract cohesive responsibilities without increasing
an existing violation.

## Required evidence before accepting implementation

- Native source tests: exact per-call association, immediate and delayed
  failure, early rejection, nested/reentrant loads, one terminal delivery,
  cancellation/replacement/destruction, 64-bit decimal round trip, and no
  status-generated command or recovery authority. Verify original API behavior.
- JavaScript tests using real signal-shaped `connect`/`disconnect` mocks:
  subscribe once; immediate signal before method callback; current failure
  displays one fixed toast; callback-before-signal; duplicate status; rejected
  call; old request after new item; Stop; destroyed/recreated player; old
  bridge signal after a new bridge; callback after disconnect; token exhaustion
  and missing `toast`. Verify disconnect uses the original signal owner.
- Exercise queue failure while another item is playing: no native stop, dialog
  removal, global error or accidental attribution to a video load. Preserve
  existing error-prompt tests and existing static/script-order checks.
- Verify both matching status and stale status cause no leakage of URLs,
  metadata, endpoint tokens, or numeric identities to UI/logs. Verify that a
  preference change retains the original request's status association without
  granting the UI command authority.

No code, UI, tests, build, browser, native helper or application was executed
for this proposal. Whether QWebChannel ordering, dependency injection, native
reentrancy and bridge replacement meet the specified contract remains to be
established with the separate implementation and acceptance evidence.
