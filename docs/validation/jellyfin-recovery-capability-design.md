# Proposed Jellyfin Web recovery transaction, v1

Status: design for review only. No Web patch, desktop implementation,
deployment, authenticated request or live playback is implied. The candidate
Web baseline is [`ea2abad3e1671473d352b3ccf06f616c61ec9381`](https://github.com/jellyfin/jellyfin-web/tree/ea2abad3e1671473d352b3ccf06f616c61ec9381)
(10.11.6); the actual user-selected producer and version remain unverified.
The pristine `playbackmanager.js` is 158,528 bytes, SHA-256
`43a9837e11041ddd624a6f68ee3fe3ffb7ee48dfa1443b27592a67c0c0895af2`,
Git blob `a5d76addb70c21fed15cc6c54dd6829d3dbf8304`. Anchors below were
corrected against those exact bytes; earlier rendered line numbers differed.
This design is limited to one seekable server video on a local player. Live,
remote-player, multi-item, queue restoration and unsupported/ambiguous source or
track identity return unsupported or ineligible. Existing ordinary playback,
queueing, autoplay and error behavior must continue when the capability is unused.

## Existing boundary and gaps

The pinned [`pluginManager.js`](https://github.com/jellyfin/jellyfin-web/blob/ea2abad3e1671473d352b3ccf06f616c61ec9381/src/components/pluginManager.js#L60)
injects both `playbackManager` and `ServerConnections` into window-registered
plugin constructors. Desktop `native/inputPlugin.js` already receives the
manager. No plugin-manager change is needed merely to expose it. This is a
specific injected dependency, not a global lookup or permission to inspect its
private state. In this candidate, `getApiClient(serverId)` filters existing
clients but can return one with absent server information
([connectionManager.js](https://github.com/jellyfin/jellyfin-web/blob/ea2abad3e1671473d352b3ccf06f616c61ec9381/src/lib/jellyfin-apiclient/connectionManager.js#L695)).
The Web-declared `jellyfin-apiclient` 1.11.0 `serverId()` and
`getCurrentUserId()` read in-memory fields without a network call
([apiClient.js](https://github.com/jellyfin-archive/jellyfin-apiclient-javascript/blob/v1.11.0/src/apiClient.js#L365));
`getCurrentUser()` can request the server and is not an equivalent getter.
An eligible candidate must positively match nonempty returned server and user
IDs; missing values or errors are ineligible. These source facts do not prove
the actual selected document's version, identity, or host binding.

The pinned [`playbackmanager.js`](https://github.com/jellyfin/jellyfin-web/blob/ea2abad3e1671473d352b3ccf06f616c61ec9381/src/components/playback/playbackmanager.js)
already provides manager `play(options)` with `serverId` and logical `ids`:
it resolves items, translates them and obtains additional parts before
`playWithIntros` ([`self.play`, line 2086](https://github.com/jellyfin/jellyfin-web/blob/ea2abad3e1671473d352b3ccf06f616c61ec9381/src/components/playback/playbackmanager.js#L2086)).
The manager owns its existing authenticated client, source negotiation, queue
and reporting. However, `truncatePlayOptions` keeps a fixed list and drops an
unknown attempt field ([`truncatePlayOptions`, line 650](https://github.com/jellyfin/jellyfin-web/blob/ea2abad3e1671473d352b3ccf06f616c61ec9381/src/components/playback/playbackmanager.js#L650));
`createStreamInfo` constructs another object without that field
([`createStreamInfo`, line 2807](https://github.com/jellyfin/jellyfin-web/blob/ea2abad3e1671473d352b3ccf06f616c61ec9381/src/components/playback/playbackmanager.js#L2807)).
Same item or URL does not identify a particular attempt. Preplay interceptors
run before bitrate and media preparation ([`playInternal`, line 2347](https://github.com/jellyfin/jellyfin-web/blob/ea2abad3e1671473d352b3ccf06f616c61ec9381/src/components/playback/playbackmanager.js#L2347));
they cannot alone prevent a continuation from dispatching after later Stop.

`PlaybackManager.stop(player)` ([line 3933](https://github.com/jellyfin/jellyfin-web/blob/ea2abad3e1671473d352b3ccf06f616c61ec9381/src/components/playback/playbackmanager.js#L3933))
returns a resolved promise if no player exists. A pending recovery must still
be invalidated synchronously at its entry. Replacement uses
`onPlaybackChanging` to stop an old player ([line 3505](https://github.com/jellyfin/jellyfin-web/blob/ea2abad3e1671473d352b3ccf06f616c61ec9381/src/components/playback/playbackmanager.js#L3505));
internal stop, user Stop, replacement, retry and natural end cannot be inferred
from a generic stopped event. The existing `cancelPlayback` destroys the current
player ([`cancelPlayback`, line 2386](https://github.com/jellyfin/jellyfin-web/blob/ea2abad3e1671473d352b3ccf06f616c61ec9381/src/components/playback/playbackmanager.js#L2386)):
a stale transaction must never reach that global cleanup path against a newer
player. Existing initial-player rejection currently invokes start handling before
a delayed error handler ([`player.play`, line 2724](https://github.com/jellyfin/jellyfin-web/blob/ea2abad3e1671473d352b3ccf06f616c61ec9381/src/components/playback/playbackmanager.js#L2724));
its promise behavior cannot serve as an attempt receipt without explicit handling.

## Proposed capability and ownership

Add a small `recoveryTransaction.js` owned by the Web playback module and an
optional, versioned capability on the **injected manager instance**. Its
explicit descriptor must identify the capability, exact supported version and
request/result schema. The host must first verify the producer document binding,
then negotiate and validate that descriptor against the supported contract.
An absent, unknown, malformed or mismatched descriptor fails closed; finding a
lookalike method alone proves neither version nor producer trust. A concrete
candidate operation is `manager.recoveryV1.begin(request)` returning a promise
of one `{ attemptId, status }`, where status is `accepted`, `rejected` or
`canceled`. This name and shape are proposed, not an existing API. The request contains an opaque,
unique attempt ID and bounded logical producer/account, server, item, source,
source-position ticks and intended paused/playing state. Track selection is
source-bound and optional only when the established checkpoint contract permits
it. No URL, credential, cached stream, live-stream handle or previous server
session is a request input. Validate schema, account/server provenance and
single seekable-video eligibility before dispatch; missing proof rejects.

The helper keeps a private manager-scoped generation and transaction record,
with a terminal latch and explicit ownership of any resources acquired for
that attempt. It does not read ambient globals or infer a pending ticket from
ordinary play state. At most one recovery transaction is current. `begin`
synchronously claims a new generation before any asynchronous preparation;
another recovery, ordinary replacement or explicit Stop invalidates the old
generation before yielding, including when `_currentPlayer` is absent. Each
attempt completes its begin promise exactly once; late results observe the begin
outcome latch and cannot change that result. That latch is separate from ongoing
request ownership: after `accepted`, retain attempt and producer-generation
correlation until a correlated native terminal outcome and settlement are
established. Stop or supersession invalidates the ongoing generation; late
callbacks, reports and cleanup from it cannot affect newer playback. If native
terminal or settlement evidence is unavailable, ownership remains unconfirmed,
not silently retired. Reuse of an attempt ID, unknown version,
account mismatch and unsupported modes fail closed.

`accepted` means the producer passed its checks and invoked the selected
player's `play` for that attempt without a synchronous throw. If dispatch
throws synchronously, settle as rejected only while the attempt remains owned;
if synchronous reentry invalidates it, settle canceled and suppress stale work.
It does **not** mean native load accepted,
media started, a frame rendered, settlement completed, or recovery succeeded.
`rejected` denotes validation/preparation failure while still owned; `canceled`
denotes invalidation by Stop/supersession before acceptance. A post-dispatch
failure is correlated separately to the same attempt, not a second terminal
result. No automatic retry after an uncertain outcome is introduced. Producer
session reporting retains its existing lifecycle and uses newly negotiated
values, including an absent play-session ID where the actual path permits it.

## Pinned manager insertion map

1. At `playbackManager` construction and `self.play`, add the optional helper
   and distinguish capability requests from ordinary calls. Validate the v1
   request and carry its opaque token in an explicit internal parameter, not
   an item equality test. `begin` initiates the manager's normal logical-ID
   route; it does not call the server directly from the desktop.
2. At `stop` and every ordinary replacement entry, synchronously invalidate the
   pending generation **before** consulting a player or awaiting anything.
   Tag internal transitions so an attempt's own replacement/stream switch is
   not misclassified as a human Stop; if that distinction cannot be established,
   fail closed. Do not alter ordinary-call results when no recovery is active.
3. In `self.play`, check ownership after each async item lookup, translation and
   additional-parts step; in `playWithIntros`, check after intro lookup and
   before queue mutation. The initial v1 route rejects multi-item/intro/remote
   cases rather than broadening its guarantee. Preserve the token explicitly
   across `truncatePlayOptions` or pass it separately; never place it in
   persisted item metadata or server reports.
4. In `playInternal` and `playAfterBitrateDetect`, check after interceptors,
   bitrate detection, old-player stop, device profile, user and stream lookup,
   and after `getPlaybackMediaSource`/any live-open continuation. Check again
   immediately before `player.play(streamInfo)` with no asynchronous gap. A
   canceled generation must bypass global `cancelPlayback`, old-player stop,
   queue/report mutation and delayed error handling that could target a newer
   request. Only resources demonstrably acquired by that canceled attempt may
   be cleaned up; if ownership is uncertain, retain/report uncertainty instead
   of stopping a shared or newer player. Initial v1 excludes live-open even
   though guarding that boundary is necessary for any later extension.
5. Pass attempt ID and producer generation **explicitly** in the selected
   player's options at the dispatch site after `createStreamInfo`; neither
   `truncatePlayOptions` nor its newly constructed object preserves them today.
   Protect success, rejection, delayed error and stream-change callbacks with
   the same ownership test. Queue, track changes, fallback retry and autoplay
   either propagate the current attempt deliberately or invalidate it before
   starting a different logical operation; they must not resurrect an old one.

The request must not recreate a URL or bypass Web's normal negotiation. Web
remains responsible for matching source and track identities, source-tick to
player-start/transcode-offset conversion, generated external subtitles, and
start/progress/stop reporting. If the requested position, source, track or
timeline cannot be proved for the fresh result, reject instead of guessing.
The desktop bridge must independently bind the injected capability to a
host-verified document generation and route the explicit attempt to its player
adapter; current W729/W730 work does not provide a fresh request or establish
this producer binding. Native acceptance and event correlation are separate
future work, not established by this Web design.

## Required tests before implementation approval

Use focused tests against the **actual patched playback manager and injected
plugin interface**, with deferred promises and fake client/player; helper-only
tests are insufficient. Cover:

- Stop before player creation, during every async lookup/negotiation stage and
  immediately before dispatch; resolve each deferred step afterward and assert
  no stale player call, queue/report mutation or duplicate completion.
- Ordinary play and second recovery supersede the first, including identical
  item/source; late old success, rejection, stop and error cannot stop, destroy,
  report for, or otherwise mutate the newer player or its resources.
- Token survives `truncatePlayOptions` and `createStreamInfo` to the actual
  selected player's options, without leaking into item metadata/server reports;
  accepted is emitted exactly once at dispatch and makes no native/frame claim.
- A lookalike method with an absent, malformed, mismatched or unknown descriptor,
  or an unverified host document binding, cannot start a recovery request.
- After accepted, Stop or replacement invalidates the ongoing generation; late
  error, end, report and cleanup callbacks cannot affect the newer player.
  Retain correlation until matching native terminal and settlement evidence;
  missing evidence leaves ownership unconfirmed without a second begin outcome.
- Invalid version, account/server mismatch or unavailable identity, nonseekable,
  live, remote, multi-item, ambiguous source/track and missing capability fail
  closed. Verify correct source ticks, selected tracks and fresh negotiation;
  verify no cached URL/session replay.
- Late preparation rejection, player rejection, delayed error, fallback retry,
  explicit Stop, replacement, next/autoplay and queue edits preserve ownership
  and terminal semantics. Exercise existing ordinary play/stop/queue/retry with
  capability unused and confirm behavior is unchanged.
- A selected player's synchronous throw and synchronous reentrant Stop or
  replacement settle once without accepting a canceled attempt or mutating the
  newly current player.
- Host-generation change and document teardown invalidate pending work before
  dispatch; selected-player options carry the matching generation/attempt. Test
  producer report sequencing with fake client and no duplicate report or cleanup.

The next review must approve the precise Web patch and its focused tests, plus
the separate host/adapter contract, before any fork, implementation, build or
deployment. This document establishes no behavior on a user-selected endpoint.
