# Healthy wake binding: offline scope

`HealthyWakeBinding` is a Qt Core-only diagnostic binding for six producer and worker steps of the injected healthy-cleanup coordinator. It does not bind libmpv, create a GUI or prove native fixture teardown. The fixture must retain the binding, callback endpoints and targets through native unregister and all required barriers. Windows execution is pending; Linux and macOS execution has not been performed.

| Coordinator step | Intended owner and bounded result |
| --- | --- |
| `CloseWakeAdmission` | Owner thread closes admission for the matching owner/generation key. |
| `UnregisterWake` | Owner invokes the injected native setter to remove the callback without holding the admission mutex. The real setter must be independently bound and qualified. |
| `DrainWakeCallbacks` | Owner requires admitted callbacks to drain before their tokens can be released. |
| `InvalidatePlayerEvents`, `InvalidateHooks` | Owner invalidates queued deliveries and hook timers for the exact key; stale work must not execute its action. |
| `FenceWorker` | Owner posts and waits for a worker-thread fence within the absolute deadline. A missing target, stale identity, unavailable worker or missed deadline is not success. |

The binding's `apply(step, key, deadline)` distinguishes done, blocked and unsupported operations. Render-context validation/free/acknowledgement and owned destruction are outside these six steps and remain unsupported by this binding. A passing pure test is not evidence of correct real callback registration, render retirement, safe QQuick teardown or production recovery. The native synchronous Stop scenario remains held.

The constructor requires an explicit native-barrier qualification: `Unqualified` or `SingleSlotSerialized`. The latter is a binary-specific proof prerequisite, not a conclusion supplied by this test. The roles share one native handle and callback slot. Only Controller may be installed directly; Player replacement requires the GUI-owner `handoff(key, deadline)`. With an unqualified barrier, unregister remains blocked and the owner must be retained.

The handoff closes the Controller delivery gate, fences the controller worker, then replaces the slot with Player and queues one Player kick. The closed gate rejects already-queued Controller deliveries; the fence ensures earlier worker work has completed before replacement. Once a valid attempt closes the gate, failure or lateness leaves it closed and the owner retained, with no automatic retry. A future fixture binding must reuse the existing Player kick in `finishMpvEventSetup`, not add a duplicate. This is an injected Qt Core path, not a copied native implementation or proof of the real callback barrier.

Controller installation and its queued delivery use the controller worker; Player replacement and delivery use the GUI/player thread. The owner serializes these transitions. Delayed hooks are context-bound to the hook target and guarded by owner/generation; deliveries or actions that post further work must carry the same guard. The injected clock and absolute deadline share a monotonic nanosecond epoch. Transition failures are published before the control transition is released, so later deliveries reject them; already-running work may remain. A blocked or uncertain operation requires the ordinary owner to retain the binding, endpoints and targets. A deadline cannot interrupt a synchronous native setter that blocks; its bounded behavior and outer containment need separate proof.
