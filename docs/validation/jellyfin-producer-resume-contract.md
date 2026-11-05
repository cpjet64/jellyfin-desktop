# Jellyfin producer and resume contract investigation

Status: public-source investigation and proposed contract, not an implementation
or a validated recovery path. Desktop source is `2f90e763e1e59bf5987a91ad7c72bffdacb01d1d`.
No account, endpoint, private page state, or live server was inspected.

## Candidate revisions and scope

The bounded candidate is Jellyfin Web **10.11.6**, release commit
[`ea2abad3e1671473d352b3ccf06f616c61ec9381`](https://github.com/jellyfin/jellyfin-web/commit/ea2abad3e1671473d352b3ccf06f616c61ec9381),
with its declared [`@jellyfin/sdk` 0.12.0](https://raw.githubusercontent.com/jellyfin/jellyfin-web/ea2abad3e1671473d352b3ccf06f616c61ec9381/package.json),
SDK release commit [`7b585b96220fae402e61c122ef301d89516c4409`](https://github.com/jellyfin/jellyfin-sdk-typescript/commit/7b585b96220fae402e61c122ef301d89516c4409),
and server **10.11.6**, commit [`10662e75e4626be71184db950ce534ab6953be77`](https://github.com/jellyfin/jellyfin/commit/10662e75e4626be71184db950ce534ab6953be77).
These are research inputs, not an assertion that the user-selected endpoint
serves these bytes, versions, or APIs. Compatibility requires independently
establishing the actual trusted producer document and supported version.

Primary source: [candidate Web playback manager](https://raw.githubusercontent.com/jellyfin/jellyfin-web/ea2abad3e1671473d352b3ccf06f616c61ec9381/src/components/playback/playbackmanager.js),
[server media-info controller](https://raw.githubusercontent.com/jellyfin/jellyfin/10662e75e4626be71184db950ce534ab6953be77/Jellyfin.Api/Controllers/MediaInfoController.cs),
and [server playstate controller](https://raw.githubusercontent.com/jellyfin/jellyfin/10662e75e4626be71184db950ce534ab6953be77/Jellyfin.Api/Controllers/PlaystateController.cs).
The candidate Web manager imports `getMediaInfoApi` and `toApi`; its SDK call
is `getPostedPlaybackInfo`, rather than a documented global playback API.
The server's [`GetPostedPlaybackInfo`](https://github.com/jellyfin/jellyfin/blob/10662e75e4626be71184db950ce534ab6953be77/Jellyfin.Api/Controllers/MediaInfoController.cs#L111)
and [`ReportPlaybackStart`, `ReportPlaybackProgress`, and `ReportPlaybackStopped`](https://github.com/jellyfin/jellyfin/blob/10662e75e4626be71184db950ce534ab6953be77/Jellyfin.Api/Controllers/PlaystateController.cs#L180)
are the relevant controller functions; the stopped handler derives the session
from the authenticated request and uses the supplied play-session ID to target
transcode cleanup. Line anchors identify the candidate source, not endpoint proof.

## Producer, source and session

The producer is the active, authenticated Web playback manager and its chosen
local player plugin, bound to one trusted document lifetime. The desktop's
`native/nativeshell.js` registers `mpvVideoPlayer` and `mpvAudioPlayer`; their
`play(options)` receives resolved stream information and calls
`window.api.player.load(...)`. That bridge is a player-facing integration, not
evidence that native code can call or identify the Web manager. The desktop has
no proven host-bound producer-document identity or request-correlated native
acceptance/events at this revision.

For a candidate server item, the manager resolves the item through its existing
`ServerConnections` client and current user, selects a player/device profile,
negotiates the media source and tracks, builds stream information, and calls
the selected player's `play(streamInfo)`. Its private helper posts PlaybackInfo
with item ID, profile, starting ticks and optional source/stream indices; the
server controller is authorized and resolves the request user. Live sources may
require `LiveStreams/Open`. Web then reports start, progress and stop through its
selected API client. The server's corresponding authorized endpoints are
`POST /Sessions/Playing`, `/Sessions/Playing/Progress`, and
`/Sessions/Playing/Stopped`; the server derives its session from the request.
The stop handler can terminate transcodes associated with a play-session ID.
Preserve the producer's reporting lifecycle; a second independent server caller
would not establish equivalent producer, queue or session ownership.

This path is conditional: the manager's nonlocal-audio fast path and preset
sources can bypass posted PlaybackInfo; local/URL items have different handling.
The candidate may extract `playSessionId` from a generated media URL, and it can
be absent. A new negotiation must use the producer's newly returned identifiers
when present; neither guaranteed freshness nor nonempty session IDs is established
for every item. Old URLs, authorization data, live-stream handles and session IDs
are not durable resume inputs.

## Position, tracks and queue

The Web manager uses 100-nanosecond ticks for source position. Its
`createStreamInfo` passes `playerStartPositionTicks` and separately records
`transcodingOffsetTicks`: for its non-HLS transcode without copied timestamps,
the latter is the requested start; otherwise its initial offset remains zero.
Its current source-time calculation adds the offset to player time converted
from milliseconds to ticks. The desktop adapters divide player-start ticks by
10,000 for `startMilliseconds`; do not apply a source offset twice or assume
all transcoding protocols use the same timeline. Prove the mapping for the
actual selected source and seek behavior before resuming.

The negotiated media source contains stream indices and defaults. Desktop video
maps Jellyfin indices to relative mpv audio/subtitle positions, treats selected
transcoded audio specially, and can pass an external subtitle `DeliveryUrl` to
the player. Re-negotiate and remap the chosen logical source and tracks against
the new media-source result; a prior index need not identify the same stream in
a changed source. Regenerate external delivery URLs via the producer, not by
replaying stored URLs. If identity cannot be matched unambiguously, do not infer
a track. Web's queue reports item and `PlaylistItemId`; queue index alone is not
a stable identity when items are inserted, moved, or removed.

## Lifecycle and required extension

Web replacement calls the existing player's `stop(false)` when reusing it, or
`stop(true)` when changing player, then continues after its promise. The desktop
video adapter's `stop` invokes native stop, emits its stopped event, and returns
an already-resolved promise; it is not proof of native settlement. Its `play`
resolves via the callback to `player.load`. Web's candidate initial server-item
rejection branch currently invokes its start path and later schedules an error
handler; stream-change rejection takes a different error path. Error fallback
may renegotiate a transcoded stream using current source ticks. Natural end can
advance the queue according to Web policy. Therefore generic stop is not proof
of human intent, and a resolved promise or an uncorrelated error/end event is
not proof that a particular request started, stopped or remained current.

No supported cross-document recovery capability with the required identity and
settlement semantics was found in the inspected interface. Proposed **v1** is
an explicit, version-negotiated capability implemented by the Web producer and
desktop bridge together, not a guessed global or private-module lookup. Bind a
host-verified producer document generation and opaque request/attempt identity
to logical server/account/item/source, queue item identity, source ticks,
selected track identities and intended playback state. The producer validates
that identity is still current, performs its own fresh negotiation and reporting,
and returns a request-correlated accepted/rejected/canceled outcome. Correlate
native acceptance, terminal events and completion to that same attempt; retain
the previous attempt's ownership until settlement is established. Replacement,
explicit stop, retry, navigation and autoplay must invalidate or supersede the
correct attempt before asynchronous continuations act. The capability must
define exactly-once completion and distinguish user stop from internal stop.
This requires a reviewed cross-repository Web/desktop change, or a documented
equivalent upstream extension. It is a proposal, not an existing API.

Before enabling recovery, qualify the actual user-selected producer version and
document binding; prove account/source and track remapping, timeline conversion,
fresh negotiation and session reporting, and races among stop, replacement,
retry, promise completion and autoplay. Include negative tests for stale producer
generations, changed queue/source, absent session or track identity, and late
native events. No live playback, authenticated request, or producer-dependent
implementation was performed in this investigation.
