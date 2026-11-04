# Qt 6.9.3 partial initialization diagnostic proposal

This is a request for review, not an implemented or approved fault seam.
`QQuickRenderControl::initialize()` in Qt 6.9.3 calls private `initRhi()` and,
on success, immediately installs that RHI into the window and initializes the
scene-graph render context. A caller cannot inject a failure between those steps
through that public call. A test which fails only after `initialize()` returns
must not describe itself as an after-QRhi, before-scene-graph-init failure.

One version-bound test-only candidate is to call
`QQuickRenderControlPrivate::get(control)->initRhi()` directly on the render
worker after GUI-created fallback injection and GL-context handoff. Before the
call, verify the exact 6.9.3 runtime and matching private headers/libraries,
expected window, null RHI, false initialized flag, and the injected fallback.
After a successful call, verify a non-null RHI, `ownRhi == true`, unchanged
fallback, and `initialized == false`; only then inject a controlled test failure.
Do not call public `initialize()` in this negative case.

The worker would then execute the reviewed partial-state cleanup with its
original context current, while the GUI is suspended and all owners remain
alive. It must observe and report whether scene-graph invalidation and the
normal worker-thread render-control destructor are valid for this exact state,
and verify actual RHI disposal, detached fallback, window aliases, and eventual
GUI-thread surface deletion. A failed assertion, unavailable context, or
unresponsive worker must remain a contained failure, never a fabricated cleanup
acknowledgment. The injected fallback and its owner must survive until the
worker has finished or the containing test process is terminated.

Calling `initRhi()` directly extends the private ABI boundary beyond the
previously reviewed surface injection and normal destructor. It is not enabled
by the helper until explicitly approved. It must first be checked against the
exact Qt 6.9.3 source and matching binaries and then exercised on native
Windows, Linux, and macOS; unavailable platforms remain blocked. The source is
[Qt 6.9.3 qquickrendercontrol.cpp](https://github.com/qt/qtdeclarative/blob/v6.9.3/src/quick/items/qquickrendercontrol.cpp#L268-L294)
and its [private initialization and reset implementation](https://github.com/qt/qtdeclarative/blob/v6.9.3/src/quick/items/qquickrendercontrol.cpp#L696-L736).
