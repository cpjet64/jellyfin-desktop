# Qt 6.9.3 threaded offscreen fixture: review proposal

This is a test-only proposal, not an implemented or validated threading path.
It applies only to the exact Qt 6.9.3 build and its matching private headers.
It does not authorize a Qt upgrade, production changes, or a dependency patch.

## Source constraint

In Qt 6.9.3, `QQuickRenderControl::initialize()` calls `initRhi()` before
initializing its render context. `initRhi()` calls
`QSGRhiSupport::maybeCreateOffscreenSurface(window)` when its private
`offscreenSurface` is null, then calls `createRhi(window, offscreenSurface)`.
For OpenGL, `maybeCreateOffscreenSurface` creates a fallback surface and its
source explicitly requires the main thread. This happens before `createRhi`
checks for an externally supplied QRhi or OpenGL context. `prepareThread()`
moves the scene-graph render context and animation controller; it does not
prepare or transfer this surface.

The destructor calls `invalidate()`, `windowDestroyed()`, deletes the render
context, and calls `resetRhi()` only if `rhi` exists. `resetRhi()` destroys an
owned QRhi and deletes `offscreenSurface` together. The private 6.9.3 header
exposes `QQuickRenderControlPrivate::get(control)`, `offscreenSurface`, `rhi`,
`ownRhi`, `initialized`, and `resetRhi(config)`. These are unsupported private
ABI, not public Qt contracts. An external QRhi changes `ownRhi`, but does not
skip fallback creation. Initializing on the GUI thread and then using the QRhi
on a worker is not established as valid: QRhi and its resources must be used
on a single thread.

Sources: [Qt 6.9.3 render control](https://github.com/qt/qtdeclarative/blob/v6.9.3/src/quick/items/qquickrendercontrol.cpp),
[Qt 6.9.3 RHI support](https://github.com/qt/qtdeclarative/blob/v6.9.3/src/quick/scenegraph/qsgrhisupport.cpp),
[QOffscreenSurface thread requirements](https://doc.qt.io/qt-6.8/qoffscreensurface.html), and
[QRhi threading](https://doc.qt.io/qt-6/qrhi.html).

## Candidate private adapter, pending review

Use a dedicated test-only adapter linked against the exact matching Qt Quick
private development package. At compile time reject every version other than
`QT_VERSION_CHECK(6, 9, 3)`; at runtime also reject a different `qVersion()`.
These checks alone do not prove private ABI compatibility: require the private
headers and libraries to come from the same pinned Qt build, and check all
private state below before any assignment. Fail closed on mismatch. Never add
this adapter or its private dependency to the application target.

1. On the GUI thread, create the window, render control, and a valid fallback
   `QOffscreenSurface` with a format compatible with the actual GL context.
   Obtain `QQuickRenderControlPrivate::get(control)` and verify that its
   `window` is the expected window, `offscreenSurface` and `rhi` are null,
   and `initialized` is false. Assign the GUI-owned fallback to
   `offscreenSurface` before any worker initialization. Keep explicit fixture
   ownership; the adapter must not permit two owners to delete it.
2. Prepare the render thread with `prepareThread(worker)` while respecting Qt
   object affinity. Create/use the OpenGL context and QRhi on that worker only;
   provide the context through the public graphics-device API. On the worker,
   make the context current, initialize the render control, create its FBO and
   render. Verify the private fallback pointer is still the injected pointer,
   and record `rhi`, `ownRhi`, and initialization outcome. Do not move a QRhi
   created on the GUI thread. GUI polish and the worker's sync require a bounded
   handshake; the GUI must not hold it while making a synchronous mpv call.
3. On the worker, after all render jobs and readbacks complete, keep the proper
   GL context current and invalidate the render control. With the worker stopped
   from accessing the fallback, detach the exact injected pointer by setting
   `offscreenSurface` to null. If `rhi` exists, call private `resetRhi()` on the
   worker with the window's graphics configuration, so owned QRhi destruction
   remains on its owning thread. For an external QRhi, its separate owner must
   destroy it on that worker after Qt has stopped using it. Acknowledge worker
   resource cleanup and join the worker before GUI destruction of the fallback.
4. Destroy the render control/window on the GUI thread only after confirming
   the private `rhi` and `offscreenSurface` are null and worker work is drained;
   destroy the detached fallback on the GUI thread. This sequence needs a Qt
   source-aware review of `windowDestroyed()` and render-context QObject
   affinity: resetting RHI alone does not prove that final destructor work is
   safe. If these requirements cannot all be met, do not implement this adapter.

The proposal assumes `QQuickRenderControlPrivate::resetRhi()` is callable and
exported with the matching private build; link and behavior must be proven. It
must not use unchecked layout offsets, a second Qt version, a global Qt install,
or a silent fallback to a single-thread render loop.

## Failure and teardown states

- Before injection: GUI destroys any surface it created; no worker owns it.
- Injection or worker start fails: ensure no worker can access the pointer,
  detach it on the appropriate synchronized path, then delete it on GUI.
- Worker context or QRhi initialization fails: distinguish null RHI, partially
  initialized render context, and a successfully created RHI. Invalidate only
  where Qt state permits; detach the injected surface even when Qt's destructor
  would skip `resetRhi()` because `rhi` is null. Destroy any created QRhi on its
  creating thread, then join and delete the surface on GUI.
- A frame, sync, readback, or retirement fails: stop new requests, complete or
  explicitly fail outstanding acknowledgments, and execute the same worker
  cleanup sequence. Do not delete a surface or context while a job can use it.
- If context-current or worker cleanup cannot be established, report a bounded
  failure and let the test process's containing job terminate it. Do not claim
  graceful cleanup, delete live objects on the wrong thread, or continue with
  another case in that process.

The exact handling of a partially initialized render context and the final
render-control destructor are unresolved review gates, not proven behavior.
No failure path may fabricate an acknowledgment or suppress a failing test.

## If private separation is not defensible

Propose a narrowly versioned Qt 6.9.3 patch that accepts an externally owned,
GUI-created fallback surface before `initRhi()`, records ownership explicitly,
and separates render-thread QRhi disposal from returning the surface to its GUI
owner. Its contract must cover initialization failure before and after QRhi
creation, invalidation, destructor ordering, and no double deletion. The patch
must be applied to a verified build-directory source copy, never the installed
Qt SDK, and reviewed for licensing and native build/distribution consequences.
No such patch is applied or approved by this document.

## Required proof before adoption

First build a Qt-only test with a changing rendered image and no mpv. Record
actual GUI/render thread IDs, context-current and FBO/readback results, repeated
independent rendering while the GUI is occupied, the bounded sync handshake,
and a natural worker join. Exercise every setup failure and teardown state above
with no surviving worker or cross-thread QObject/surface warnings. Verify exact
Qt version, private-header/library identity, and the absence of private adapter
symbols from the production app. Run native Windows, Linux, and macOS tests on
their actual backends; unavailable platforms remain blocked. Only after this
proof and review may existing C2, offscreen playback, or MPRIS fixtures migrate.
This establishes a test mechanism, not GPU recovery or production playback.
