# Initial bridge opening scope

The host constructs the window, view, channel, and endpoint before dispatching
one initial client URL. A one-use host opening and two separately observed
events gate a provisional endpoint handshake. URL equality is a consistency
check only. The opening marker is not carried by Qt's navigation or loading
events, and those events do not establish a document identity or their origin.

A same-URL event queued before host dispatch can arrive afterward and satisfy
an observation check. The resulting handshake means only that the endpoint
for this view and channel responded; it is not producer authentication, native
playback acceptance, or recovery qualification. Capability remains unsupported,
the adapter's candidate remains null, and automatic recovery is not enabled.

An unexpected or repeated main-frame request or load start, redirect, failure,
stop, renderer loss, channel replacement, object destruction, or accepted exit
permanently revokes the endpoint. Success cannot recreate or revive it. A
pre-observed navigation also revokes recovery; the host may still perform its
single configured ordinary startup dispatch without creating a new endpoint.
The actual WebEngine event timing and native playback remain unqualified.
