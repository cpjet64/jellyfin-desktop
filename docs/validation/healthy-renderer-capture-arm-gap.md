# Healthy frame capture: renderer arm gap

Status: source finding for review at integration Desktop
`2ec938cd168a831a0894eebc71e39263789137bb`. The diagnostic graph/session is
incomplete; no native playback, process replacement or CI is authorized here.

## Observed boundary

The diagnostic renderer capture patch
`patches/healthy-restart/0001-render-capture.patch:18-26` requests a mailbox
ticket using the actual `MpvRenderer` object's address and only attempts the
capture when the active ticket matches and the actual FBO is 160 by 90 pixels.
`tests/fixture_restart/frame_capture.cpp:22-30,41-44` arms the mailbox with an
expected renderer value and only returns a ticket for that exact value. The
capture publisher also rejects images that are not 160 by 90.

Qt creates the renderer through the copied item's `createRenderer()` path
(`patches/healthy-restart/0003-render-retirement.patch:38-44`). The fixture's
retained Slot/Lifetime records the original render thread, GL context and
generation, but does not expose the actual Qt-created renderer identity to the
graph. The later copied renderer patch retains independent render-context state
(`patches/healthy-restart/0004-async-teardown.patch:70-80,121-126`); it does not
by itself establish a safe graph-to-mailbox arming operation.

Consequently, the graph cannot currently arm the mailbox with the renderer that
will actually publish the frames. A GL-context address, item address, guessed
renderer pointer or identity from a different process is not equivalent. A
bare renderer address captured before arming is also insufficient: the object
may have been destroyed and its address reused. Requesting a 160 by 90 *logical*
window or item does not prove a 160 by 90 physical FBO, especially under DPI
scaling. Missing tickets or mismatched actual dimensions must remain failure,
not evidence of a paused hold or advancing playback.

## Candidate for review

Have the actual copied renderer issue an opaque, generation-bound capture-arm
capability through its retained state. The private graph operation would arm
only while that same renderer lifetime and original render thread/context epoch
remain valid; renderer destruction, context loss, generation replacement or
retirement must invalidate the capability and disarm or reject its queued
captures. Use a weak/non-owning observation where appropriate so the capability
does not fabricate ownership of an already destroyed Qt object. Keep mailbox
operations serialized with the existing render/retirement admission boundary;
do not hold a coordinator lock across native free or introduce another renderer
or event consumer.

Headless tests should use the actual arm/retire boundary with injected render
operations to cover original renderer and epoch, stale identity and pointer
reuse, destruction before/during arm, concurrent retirement, late queued frame,
disarm, mismatched physical FBO and capture overflow. The real backend still
requires separately authorized observation of actual renderer/context lifetime,
FBO dimensions and frames. This proposal does not authorize an implementation
or a native run.
