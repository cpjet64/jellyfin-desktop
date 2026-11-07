# Diagnostic restore failure cleanup proposal

This proposal covers an armed B fixture when a malformed control or Grant frame,
endpoint loss, or another local protocol failure revokes restoration. It is a
design checkpoint, not a claim that the failure path is implemented or that the
fixture entrypoint is enabled.

## Retained B failure boundary

For the source and headless diagnostic slice, an internal failure must preserve
the first cause, cancel the same retained ChildGate, and keep owner-thread access
safe when normal Graph access is unavailable. Pending owned I/O must remain
settleable. A contained claim stays contained: failure accounting does not
authorize another Stop, renderer free, or `Closed` result. The failure-accounting
source checkpoint is [6b391bcf](https://gitlab.curtpme.com/cpjet64/jellyfin-desktop/-/commit/6b391bcfd2ecdc9e647becb7d72574d2ff58f971);
the contained pending-acknowledgement tests are in [a633d621](https://gitlab.curtpme.com/cpjet64/jellyfin-desktop/-/commit/a633d621191cae7e9af17a826f0f935c1b29cc6e).
These references identify source changes, not a claim that their runtime checks
have passed. Qualified Graph destruction is tested separately from this retained
B failure path.

B `InternalFailure` cannot currently establish qualified renderer and core
retirement. Retain the ownership graph for bounded outer-process containment
when that qualification is unavailable. Before a runnable A-to-B demonstration,
review the terminal lifecycle for both successful and failed B sessions; process
termination does not establish legal renderer retirement.

The existing authenticated cancellation path carries a parent-authored sequence
and may produce a correlated cancellation result. A local protocol failure has
no such sequence. It must not invent one or emit `CancelSettled`, `CancelFailed`,
or `Closed` as if a parent cancellation had been received.

The proposed private Player cleanup state records provenance as either
`AuthenticatedCancel` or `InternalFailure`. The first retains the actual wire
sequence and follows the existing result protocol. The second records no wire
sequence and uses a separate private begin method, but both paths claim the same
one-shot cleanup operation and use the same Player receipt ledger, event
consumer, native Stop, deadlines and terminal checks. Neither path can submit a
second Stop or use a synchronous fallback.

On failure, the existing child owner immediately cancels the actual ChildGate
and closes normal restoration admission. If the Player was armed, the graph retains
the real arm identity obtained from the Player and starts internal-failure
cleanup only while that same attempt owns the core. The owner keeps the graph,
callback storage and sole event consumer alive and polls the existing cleanup
state through the original overall deadline. After a qualified `NoLoad` or
`Stopped`, the owner may proceed to its separately qualified retirement path;
that cleanup result alone is not `Closed` and does not make the demonstration
successful. No cancellation result is sent for this local provenance.

An unknown arm result, missing retained identity, lost ownership, invalid module,
unsettled operation, failed cleanup or expired deadline remains unqualified.
The graph and uncertain native state remain under the existing containment
contract; a timeout does not become an acknowledgement. An admitted load with
no observed START remains unknown unless a separately qualified native ordering
barrier excludes later activation.

The child owner retains cleanup-only graph access when the same claim remains
eligible. A revoked or contained claim leaves cleanup Unknown and ownership
retained for outer containment. The design needs
independent tests through the actual child, graph and Player boundaries for
failure during arm, load, seek, unpause and native callbacks; reentrant failure;
superseding intent; deadline expiry; and delayed or duplicate results. Tests
must show zero restore dispatches after revocation and no successful terminal
wire result for internal failure. Native execution, CI and production recovery
remain outside this checkpoint.
