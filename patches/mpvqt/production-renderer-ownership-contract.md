# Production renderer ownership contract (proposal)

This document specifies a pending implementation. It does not enable recovery
or establish that the current MpvQt production path satisfies this contract.

## Ownership and cancellation

Each renderer generation has shared state containing its immutable generation
identity, callback admission and in-flight accounting, terminal state, and a
lease on the mpv core. The render-side owner alone owns the render context and
records the creating thread and OpenGL context. GUI observers hold revocable
authority, not ownership of the render context. No scheduled job or callback
may capture a raw item, window, controller, or renderer pointer whose lifetime
is not independently guaranteed. Item/window destruction revokes observer
authority; queued notifications validate that authority and generation again.

The current item destructor owns and destroys the core. Before implementation,
replace that lifetime boundary with an explicit core lease that survives all
render callbacks and render-context ownership. Core destruction must occur
only after confirmed callback quiescence and context retirement. If the
original context is unavailable, neither a context free nor core destruction
may be claimed. The concrete bounded retention and shutdown behavior for this
case needs independent review; a deliberate resource leak cannot silently be
reported as successful cleanup.

## Retirement and replacement

The GUI request carries the current generation and original absolute deadline.
Only one request for that generation may enter pending. Render work captures a
shared owner, is cancellable by revoking its request authority, and checks both
the original render thread and the exact original current OpenGL context before
any context operation. Close admission, unregister the callback, and prove all
admitted callbacks have returned before freeing the old context. No callback
may enter or re-arm an obsolete generation. Failure, cancellation, a dropped
job, unavailable render thread, or context mismatch is blocked or failed, never
a retirement acknowledgement.

After confirmed retirement, explicitly restore video output disabled by mpv
render-context free, check the operation result, create a new render context on
the qualified render thread, and register a callback bound to a new generation.
Retain the same mpv core, controller binding, and single event consumer. A new
generation or successful create is not a fresh-frame acknowledgement. A
successful acknowledgement requires an actual new-generation video update and
successful rendering of the resulting frame, including the paused-first-frame
case. Define and qualify exactly which mpv and Qt observations prove this before
connecting them to the watchdog; elapsed time, audio progress, vo-configured,
FBO existence, or an old-generation callback are not frame proof.

## Qt boundaries and review gates

Qt documents the framebuffer renderer destructor as running on the rendering
thread, and `synchronize()` as the safe item/renderer exchange point. It does
not make the original context current merely because a renderer destructor
runs. A scheduled render job may be destroyed without running when a window was
never shown. Scene-graph invalidation normally has the window context current,
but explicitly excepts externally lost contexts. Therefore every cleanup path
must check the actual current context and must handle a job that never runs.

References: https://doc.qt.io/qt-6/qquickframebufferobject-renderer.html and
https://doc.qt.io/qt-6/qquickwindow.html.

Before enabling the normal bundled path, independently review the lease and
callback barrier, bounded handling of unavailable original contexts, explicit
video restoration, real-frame evidence, and lifetime ordering under item/window
destruction. Qualify system MpvQt separately or report unsupported. Existing
diagnostic patches and simulated observations are not production acceptance.
