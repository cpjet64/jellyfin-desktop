# Healthy cleanup coordinator: proposed binding

The standalone `HealthyCleanup::Coordinator` is a pure, injected ordering contract. Its tests do not run Qt, mpv or a native fixture and do not establish that any concrete adapter satisfies these obligations. The synchronous Stop native scenario and existing render holds remain in force. Application and package targets are not enabled by this contract.

The caller owns the resources and supplies nonthrowing `Operations`, a fixed absolute deadline, scenario Stop result, context classification and generation/thread/context identity. `runScenario` catches scenario exceptions while the external owner remains alive, preserves recorded input facts, and invokes the coordinator once with the original deadline. Reentry is rejected; this is not cross-thread synchronization. Each operation must complete within the deadline. A retained owner does not prove its resource graph remained intact after a free attempt. A failed or late `DestroyOwned` can have partially destroyed it and is reported as ownership uncertain for outer containment.

| Coordinator operation | Proposed executor and acknowledgement owner | Adapter obligation |
| --- | --- | --- |
| `CleanupStop`, `Terminal` | GUI owner / controller | When requested, perform one cleanup Stop and report its actual result separately from scenario Stop; prove idle/no VO and force-window state, or prove no core/playback existed before binding. |
| `ValidateNoContext` | GUI owner with render owner evidence | For `Context::Never`, prove no context or callback was ever installed. `Unknown` does not qualify; no free or render acknowledgement is inferred. |
| `CloseRenderAdmission`, `ValidateRenderContext` | Render owner / original render thread | Close admission and verify the supplied generation, original render thread and current original context before retirement. |
| `UnregisterRender`, `DrainRenderCallbacks`, `RejectQueuedRender` | Render owner and callback admission owner | Unregister without holding an admission mutex, retain callback tokens through drain, and reject stale generation work. |
| `FreeRender`, `ClearRenderPointer`, `AwaitRenderAck` | Original render thread; GUI owner receives identity-matched ACK | Free returns void: the report records attempt and return, not a native success code. ACK must match the supplied identity and follow free return and pointer clear before deadline. |
| `CloseWakeAdmission`, `UnregisterWake`, `DrainWakeCallbacks` | Controller / wakeup admission owner | Close and drain the separate wakeup producer; unregister without holding its admission mutex. |
| `InvalidatePlayerEvents`, `InvalidateHooks`, `FenceWorker` | GUI Player owner / controller worker | Invalidate only this owner and generation, then fence worker activity, including external-events-OFF; do not drain global or unrelated queues. |
| `CloseSecondaryObserver`, `DestroyOwned` | Secondary observer owner, then GUI resource owner | Close the observer before core destruction. Destruction is attempted only after prior steps and deadline checks; failure or overrun is not a successful teardown. |

An actual binding needs independently reviewed private access, thread/context and callback lifetime evidence, finite native process containment and failure-path tests. No acknowledgement may be synthesized by the adapter merely to make the coordinator pass.
