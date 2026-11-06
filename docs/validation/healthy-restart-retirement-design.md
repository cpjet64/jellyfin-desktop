# Healthy restart: render-thread retirement design

Status: **design only; native fixture remains held**. This proposes a diagnostic-only
change for the separately copied, external-events ON healthy-restart target at
Desktop `4e743682335554ca0117fdfb0374615f6cdbca75`. It does not authorize
implementation, native execution, production enablement, or a change to the
existing OFF/C2 fixture, its wake owner, or its callbacks. Processes A and B
each own their own core, item, renderer and GL resources; no native handle,
renderer generation or GL object crosses between them.

## Observed boundary and dependencies

The checked-in gitlink selects MpvQt `2ebbd38bf65d01be9c55d673ecd43c8bb9a6fc8e`.
In its `src/mpvrenderer.cpp`, `createFramebufferObject()` creates the mpv
render context with the current Qt OpenGL context, installs `on_mpv_redraw`
with the item as callback data, and `render()` uses the context. That callback
queues an item update; it is distinct from the primary mpv client-event
consumer. The healthy capture patch adds render update/info and capture around
the existing render call. It is not a retirement protocol. The current
`MpvAbstractItem` destructor calls render-context free and later terminates
the core, without establishing that Qt's original GL context is current or
that render and callback work has retired. See
[healthy-restart-lifecycle.md](healthy-restart-lifecycle.md) and
[capture patch](../../patches/healthy-restart/0001-render-capture.patch).
The isolated design worktree has no initialized MpvQt submodule: the source
inspection used the integration worktree and its checked-in gitlink. The
audit's ON-renderer and capture-patch normalized hashes are recorded in the
linked lifecycle document; a future build must independently verify its
actual pinned inputs and generated outputs.

The [pinned mpv render header](https://github.com/mpv-player/mpv/blob/01e2533f3143b8940857ca419b0b367f583e05ca/include/mpv/render.h)
requires serialized render calls, the same current GL context for the OpenGL
backend, and freeing the render context before destroying its mpv core. The
installed Windows header is consistent with those rules, but its independent
pin provenance was not established here. Render calls must not run from the
update callback, and the render thread must not wait on a thread executing
unsafe libmpv calls. `mpv_render_context_free` itself can block; a deadline is
not an interrupt. The header does **not** by itself prove that unregistering a
callback closes the pre-entry race or drains callbacks already entering.

[Qt's render-job contract](https://doc.qt.io/qt-6/qquickwindow.html#scheduleRenderJob)
provides a candidate render-thread entry with the renderer's GL context current;
it does not guarantee a job will run. A job can be discarded when the window
is not renderable or before a scene graph exists. Scene graph invalidation,
context loss and teardown can change what is current. The implementation must
verify the selected Qt 6.9 build and actual render-loop behavior; scheduling or
a renderer destructor alone is not an acknowledgement.

## Ownership and state

The fixture's outer owner holds the window, item, controller/core, renderer
retirement record, callback closure and all queued-job state until one terminal
outcome is recorded. The copied item and renderer share one per-attempt record;
it contains a monotonically unique local attempt/generation, lifecycle state,
creation render-thread identity, original `QOpenGLContext` identity plus a
non-reusable context-lifetime epoch, context pointer, callback admission and
in-flight counts, and bounded result/timestamps. This is internal fixture
evidence, not a transferable identity or a public API. A bare address alone
does not prove the same context after destruction/reuse. The record and callback
token outlive every native callback and posted job that can reference them.

The serial state machine is:

| State | Meaning and permitted transition |
| --- | --- |
| `Uncreated` | No render context or callback has been installed. Close creation admission before accepting an independently proven no-context terminal outcome. Unknown or partially created is not `Uncreated`. |
| `Active` | The creation path recorded a successful context, original thread/context/epoch and callback installation before exposure; render and capture are admitted only for this record. |
| `Closing` | Owner has requested retirement once, stopped new scenario work and closed render/callback admission. Queued redraws and framebuffer-creation requests carrying this or stale generations reject before item/core access; no new render context may be created. |
| `Draining` | On the original render thread, the matching callback is unregistered without holding the admission lock. Previously admitted callbacks and any already executing render operation must finish. The closure remains alive until a qualified native unregister barrier also excludes callbacks that obtained the pointer before entry. |
| `Freeing` | Only one render-thread operation, after closure/drain and a fresh same-thread, same-live-context/epoch check, owns the sole render API slot. It calls `mpv_render_context_free` exactly once, then clears the matching item pointer and records completion. |
| `Acknowledged` | The owner receives a generation-matched success record published **after** free returned and the pointer was cleared. No subsequent queued work may use or recreate that context. |
| `Retained` | Timeout, wrong thread/context, loss, callback-barrier uncertainty, scheduling failure, failed/late operation or inconsistent state prevents success and dependent destruction. The complete resource graph remains held for outer fixture containment. No retry, replacement context or destructor fallback is silently substituted. |

Transitions are monotonic, single-owner and exact-once; duplicate requests,
late completions and stale generations cannot turn failure into success. If a
free call has begun but has not returned, ownership is **uncertain**, not
proven intact. The outer boundary retains the process/resource graph; it must
not claim the core is safe to destroy or free the context again. Context loss
before the free call similarly leaves the graph retained. If Qt or another
owner already destroyed a dependent object despite the hold, classify ownership
uncertain and fail; retention cannot retroactively restore it.

## Closure, render operation and acknowledgement

1. The fixture stops accepting new scenario operations under its original
   absolute monotonic deadline. It maintains the existing ON-mode Player as
   the only primary event consumer. Follow the independently qualified Stop,
   wake/event closure and controller-worker ordering; a render wake callback
   is not a client-event callback. Do not block the GUI event loop waiting for
   the render job or issue a blocking libmpv call on the render thread.
2. Close creation, render and callback admission in shared state before
   posting the retirement request. A callback uses a separately retained token,
   records entry/exit, does no mpv or GL work, and posts only a guarded update;
   queued deliveries check the closed state and generation before touching the
   item. Counter increments alone do not solve a callback that acquired a raw
   pointer before the increment. Qualify the exact pinned mpv callback setter's
   unregister/dispatch synchronization from its implementation and build;
   if no barrier can prove no future entry, keep the token and owner retained
   and do not acknowledge. Never assume disconnecting a Qt signal drains it.
3. Request a one-shot render job for the existing visible scene and trigger an
   update where necessary. The job captures only the retained record, not an
   unguarded item pointer. Its entry checks state, generation, original render
   thread, current original live GL context and epoch. It serializes against
   render, update, info, framebuffer creation and free; it never waits while
   holding a lock needed by callbacks, the GUI, or mpv. If an admitted callback
   is still active, return and arrange another bounded poll/job rather than
   blocking the render thread. An absent job or invalidated context is failure,
   not permission to run the free on the GUI thread or a shared/new context.
4. After native unregister is qualified and all admitted work is observed
   drained, recheck context and deadline immediately before the one free.
   Clear the exact context pointer on return while the item is still owned.
   Publish an immutable success record to the GUI owner only after those steps;
   the owner accepts it only for this attempt and before the original deadline.
   The record includes the creation and free thread/context/epoch, callback
   closure and drain result, free count and completion time. A posted signal,
   renderer destruction, process exit or zero callback count alone is not it.
5. Only after that acknowledgement, and after the separately required Player
   event/wakeup closure, queued-work rejection, worker fence and observer
   closure have succeeded, may the owner destroy the item/controller/core in
   their valid order. A proven no-context branch has its own evidence and must
   prevent a concurrent or queued creation. The primary failure and cleanup
   failure are recorded separately; neither can be replaced by a later success.

All checks and posts use the **same original overall deadline**, including the
reserved cleanup boundary already owned by the fixture; no per-stage timeout
extends it. A timed-out synchronous native call may still be running. An
independently owned outer fixture process/job must retain and contain that case;
do not kill a worker thread, free in-flight storage, or report healthy teardown.
If the original render thread is the GUI/control thread, or the render loop
cannot make progress independently while required core work is outstanding,
block before the scenario. No lock cycle may require GUI or mpv progress while
the render thread waits; callback and acknowledgement paths never synchronously
wait on one another.

## Acceptance before a native request

Future implementation must use the new healthy ON copied sources and matching
headers/MOC, retain a verified source and object graph, and reject mixed or
unverified inputs. The old fixture and production targets remain untouched.
Focused deterministic headless tests, registered on Windows, Linux and macOS,
must exercise the actual shared retirement logic with injected render/context,
clock and callback boundaries, including:

- exact-once success and ordered pointer-clear/ack/item/core destruction;
- in-flight render and callback, callback pre-entry versus unregister, queued
  stale redraw/create work, duplicate requests and no-context admission race;
- wrong/replaced/lost context or thread, hidden window/job never run, late job,
  deadline expiration before/during/after free, callback-barrier failure and
  a blocked free: no success or dependent destruction, owner retained;
- first failure and cleanup evidence preserved across ordinary return and
  exception; duplicate/late acknowledgement cannot reverse the outcome;
- separate A/B instances and old OFF/C2 route unchanged, no alternate mpv event
  consumer, no context/core/renderer handle transferred between processes.

These tests model explicit contracts; they do not establish a real GL or mpv
callback guarantee. A separately authorized native qualification must record
actual Qt render thread and context lifetime at creation and free, callback
unregistration/drain and render exclusion, free return and pointer clear, GUI
acknowledgement, and later item/core destruction, including a forced failure
case under owned process containment. It must run on each claimed platform and
graphics backend; headless tests or process exit are not substitutes. This
design neither establishes visible playback nor enables application recovery.
