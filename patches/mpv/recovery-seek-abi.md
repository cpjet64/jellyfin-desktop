# Private owned-seek bridge

The optional V2 render observation preserves the V1 prefix and appends an
opaque 64-bit operation ID. Zero means that the selected frame is not evidence
for an owned seek. It does not prove Qt submission or visible presentation.
Consumers retain the entire native frame tuple and correlate it with the exact
later submission; a timestamp or an unkeyed event is not a substitute.

`mpv_recovery_seek_async_v1` accepts reply userdata, a nonzero operation ID,
the expected native playlist entry and an absolute target in milliseconds.
The normal command reply reports whether the core admitted that request; it
does not report a completed seek. The existing event consumer receives it.
`mpv_recovery_seek_cancel_v1` synchronously advances the shared epoch and
returns an error when the epoch is permanently exhausted. Each new request
advances the epoch too. Call cancellation before Stop, changing entry, or any
intent that invalidates an admitted operation. Callers must fail closed on an
error and must not reuse operation IDs within the lifetime of their evidence.
If the async API has reserved a reply when exhaustion is reached, it returns
successful dispatch and sends a negative correlated command reply; no seek is
admitted. The synchronous cancellation API reports exhaustion immediately.

The core rechecks the entry, playback state and epoch before accepting the
request and again before seeking. A compare-and-exchange claims application:
if revocation wins first, the queued seek is discarded. If application wins,
it has linearized before a later intent, although demux work may still be in
progress. Later revocation invalidates its evidence; it cannot retroactively
cancel work already claimed. The operation is latched only after successful
demux seeking and the associated playback reset. Untracked seeks and video
resets revoke the epoch. Each selected frame retains that epoch and operation,
and the render path checks the epoch before returning the ID. Consumers must
also reject observations superseded after the native check.

The new request revokes older owned evidence when its epoch is allocated; its
admission replaces any older queued seek. The admission check and queue
replacement run on the core thread. A newer
untracked seek either runs first and revokes the request before admission, or
runs afterward and revokes and replaces its queue entry. Cancellation that
races after the admission check can prevent the later application claim; it
does not restore the older, already superseded queue entry. Admission and the
later application claim are separate ordering points.

The V1 symbol and ordinary render entry point remain available. Resolve all
three owned-seek symbols from the same loaded module as ordinary mpv. When the
complete capability is absent, use the existing V1 resolver for observation
and ordinary commands; do not claim correlated seek completion. This is a
source-only contract until the patch is built and independently validated.

The application resolver discovers private V1, V2 and seek symbols only when
`JELLYFIN_VERIFIED_RECOVERY_KIT` is defined as numeric 1 by the verified build
configuration. An absent flag or any other value disables private discovery
and leaves ordinary rendering available. The flag alone does not establish
capability: each required symbol must still resolve from the same loaded mpv
module and returned observations must pass the existing validation.

The subsequent epoch-test patch registers unit cases against the same private
atomic helpers used by the core and render paths. These cases cover ordering,
revocation, stale epochs, and exhaustion only. They do not exercise core-thread
entry checks, demux failure, frame production, software rendering, or actual
concurrent scheduling. Those require a separate headless libmpv integration
harness with controlled media and scheduling.
