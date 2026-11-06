# Healthy restart renderer: source boundary

This is a source audit at Desktop commit `9a0e8362e09596437649de47978a1f34aa47031a`,
with MpvQt submodule `2ebbd38bf65d01be9c55d673ecd43c8bb9a6fc8e` and the
repository's external-event ownership patch. It is not a native execution or
teardown result. The new healthy-restart copy uses external events and must
remain separate from the held OFF-mode FixtureWakeOwner/C2 fixture.

The selected MpvQt `MpvRenderer::createFramebufferObject` creates the mpv
render context and registers `on_mpv_redraw`, passing the item as callback
context. The renderer's `render()` uses that context and its framebuffer.
The callback queues `MpvAbstractItem::update()`; it is a rendering wakeup, not
the Player's primary mpv event consumer. The source does not expose a
fixture-owned retirement acknowledgement from this renderer.

The [capture-only patch](../../patches/healthy-restart/0001-render-capture.patch)
adds an update query, next-frame information query, actual render return and
optional framebuffer copy. It continues the render even when no capture is
armed. The recorded context, thread and render serial describe that render
pass; flags and serial do not prove fresh decoding or safe lifetime. A capture
is accepted only by the separate frame-acceptance rules, and this patch does
not establish a live renderer-to-item identity or 160-by-90 framebuffer.

`MpvVideoItem::~MpvVideoItem()` releases the Player's controller binding in
external-event mode. The selected `MpvAbstractItem::~MpvAbstractItem()` then
calls `mpv_render_context_free`, clears the mpv wake callback, stops and joins
the worker, and destroys the mpv core. These are source-level calls, not proof
of the ordering or completion of renderer retirement, in-flight callbacks or
queued work. In particular, there is no observed acknowledgement that the
renderer has retired and no established original GL context at the item-side
free. Qt documents that its framebuffer renderer is deleted on the rendering
thread; the pinned [mpv render contract](https://github.com/mpv-player/mpv/blob/01e2533f3143b8940857ca419b0b367f583e05ca/include/mpv/render.h#L52-L67)
requires the original OpenGL context current for render API calls. See the
[Qt 6.9 renderer documentation](https://doc.qt.io/archives/qt-6.9/qquickframebufferobject-renderer.html).

The capture patch cannot close this lifetime gap. Before any native A-to-B
attempt, a separately reviewed implementation must establish callback closure,
actual renderer retirement acknowledgement, original-context render-context
free, and item/core destruction in a proven order. Missing or uncertain
acknowledgement must keep dependent resources owned and fail the healthy case.
Process exit is not that acknowledgement. No C2 retirement, reconstruction or
FixtureWakeOwner takeover is introduced by this capture patch.

For the pinned ON renderer, LF-normalized SHA-256 before capture is
`b519e71867813e8bcb73ef4353df1d32700e0ca988575c1e2f4017c1d1d1b798`;
the normalized patch is `cb95ccdcf16a887fa0ad53b340d1be3d0b8b861cf313b65f39d41b30b43ba7da`,
and the patched renderer is
`ee623a28c4bb694f85280d32708f54d7426a0a3c475deb73c8f97f6c5ce9d90d`.
The separate copied-source build must verify these inputs and its complete
object, include and MOC graph; this note does not certify that future build.
