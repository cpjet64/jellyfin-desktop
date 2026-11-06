# Healthy graph lifecycle: prerequisites for concrete composition

Status: **design and source inventory only** at Desktop `fef34ae63e5b460c8a03c5d8d957bdbd5d34fe8d`.
The healthy restart executable remains a held exit-78 placeholder. This document
does not authorize native execution, CI, production changes or OFF/C2 changes.

## Concrete blockers

The healthy build copies the pinned MpvQt source and applies the diagnostic
patches; the fresh W881 generated copy at integration worktree
`deps/w881-healthy-build/healthy-restart-copy`, built from Desktop `7f5d64d`,
shows the following behavior. The generated copy is inspection evidence, not a
substitute for revalidating the actual copied inputs in a later build.

* `mpvqt/mpvabstractitem.cpp:27-46` constructs the controller and its private
  worker thread, starts the worker, and waits in `Qt::BlockingQueuedConnection`
  for `MpvController::init`. Constructing the item on its required GUI thread
  can therefore block that thread. The controller's `mpv` handle is read only
  after the synchronous initialization returns.
* `mpvqt/mpvabstractitem.cpp:52-66` unregisters the wake callback, calls worker
  `quit()` and unbounded `wait()`, and then terminates the core in the item
  destructor. The healthy retirement patch adds an admission guard but does not
  replace the blocking wait or expose a worker-completion operation. The worker
  pointer is private in `mpvqt/mpvabstractitem_p.h:20`.
* `tests/HealthyWakeBinding.cpp:210-226` posts a worker fence and waits on
  `QSemaphore::tryAcquire`; `apply` requires the Player/GUI thread. Its native
  setter is also synchronous and may block. That binding is a tested producer
  model, not evidence of an asynchronous, bounded live adapter.
* `src/player/MpvVideoItem.cpp` queues initial item property commands in its
  constructor and, in external-events mode, attempts Player binding and
  `initializeMpv()` from `setPlayerComponent()` without waiting for `ready`.
  `src/player/PlayerMpvEvents.cpp` requires binding and release on the Player
  thread. Any asynchronous core initialization must preserve these contracts
  explicitly; merely changing the queued connection does not do so.
* The item also offers blocking controller wrappers in the W881 copy at
  `mpvqt/mpvabstractitem.cpp:105-159`. The held executable does not exercise
  them, so a future graph must inventory its chosen calls rather than assume
  every wrapper is on the fixture path. `FixtureRestartAdapter.cpp:129` does
  invoke `stopMpv()`, whose default `PlayerComponentCommands.cpp:93-105` path
  calls synchronous `mpv_command`. Restore seek and native receipt/observation
  dispatch use `mpv_command_async`; these are separate from the synchronous
  startup, stop and teardown calls. No full-application no-blocking claim is
  established by this inventory.

The native callback barrier remains disabled until the loaded module identity
is verified and the applicable render and wake contracts are independently
qualified. A local callback count or a source-only proof cannot establish that
the running library matches that proof.

## Smallest dependency proposal

Use a diagnostic-only copied-item lifecycle API, leaving the pinned submodule,
production targets and OFF/C2 path unchanged. Allocate the outer containment
lease before starting the item/core and retain every partially initialized object,
callback token, pending job and worker until a terminal outcome. The outer
process watchdog owns the original monotonic 180-second deadline; the normal
scenario ends at 150 seconds, reserving 30 seconds for containment. A GUI timer
is useful for progress but is not the watchdog.

1. **Startup scope for review:** if the no-GUI-wait requirement applies to
   fixture construction as well as retirement, start core initialization
   asynchronously on the controller worker. Publish
   an explicit, generation-bound result to the GUI thread only after the handle
   is available. Preserve the existing `ready` and early queued-property ordering;
   connect the Player and perform its event setup exactly once on the Player
   thread after readiness. Failed posting, initialization, loss, or deadline
   yields retained failure, including partial-construction cases.
2. **Retirement minimum for review:** expose a diagnostic worker lifecycle boundary for the *actual* private worker:
   close admission, queue an asynchronous fence, observe its matching completion,
   request quit once, and observe the worker's terminal state without waiting on
   the GUI thread. Treat a missing, late, wrong-generation or failed completion
   as uncertainty. Keep the controller and queued captures alive until the
   worker's outcome is observed; do not infer closure from a caller-supplied bool.
3. Replace the GUI-thread wake-fence wait with a generation-scoped asynchronous
   operation on the same real binding, preserving one Player event consumer.
   The native wake setter and render unregister/free may block; do not move an
   unbounded setter onto the GUI thread or terminate a worker to escape it.
   Closure requires the qualified native pre-entry barrier, invalidated Player
   events and hooks, worker fence, and secondary-observer closure independently
   of the render acknowledgement.
4. Only after the exact matching render free has returned and been acknowledged,
   the separate Player/worker closure is observed, and owned resources are still
   intact may the graph approve destruction. The copied item destructor must not
   perform an unbounded GUI-thread wait or improvise a render free. An absent or
   inconsistent completion retains the whole graph for the outer watchdog.

Ask Pro to decide whether the authorized minimum requires the startup rewrite,
or only the retirement closure path, and to identify the treatment of the
fixture's synchronous stop. These options are not interchangeable, and neither
implies that unrelated application commands must be rewritten.

This is a proposed dependency, not an assertion that the current APIs already
provide it. In particular, `healthyApproveDestruction(true)` cannot be used as
proof that a worker actually closed: the future graph must derive that fact
from the worker and producer boundaries above.

## Restart and acceptance boundaries

A's frozen intent must preserve the captured desired playing/paused state and
revision while its internal replacement stop is checked against the same frozen
receipt, observation, asset and checkpoint. Stop alone cannot authorize B:
matching render retirement acknowledgement, real Player/worker closure, and
unforced settlement of A and all owned descendants are required first. The
one-use parent-bound logical grant may carry asset hash, position, paused state,
run, grant and revision. It must not transfer A's process, core, controller,
renderer, native handles, thread/context identities, callback storage or local
generation into B. B creates fresh local identities and a new graph. User stop
or new play revokes the grant before any subsequent load or seek.

Independent headless tests should invoke the actual copied-item and graph
orchestration through injected operations at their real external boundaries,
not a second modeled state machine. Required cases include asynchronous init
and property ordering, one Player consumer and kick, partial creation and
unexpected QObject loss, failed/dropped/late fence and worker completion,
blocked native calls, callback pre-entry, same-thread render rejection,
deadline before/during/after free, and a late or stale acknowledgement. Verify
that no path destroys the graph or grants B from Stop alone, that the accepted
path requires the exact ACK plus real closure and unforced settlement, and that
frozen playing/paused intent survives only until a user revision revokes it.
These tests do not qualify Qt, libmpv or visible playback on a real backend.
