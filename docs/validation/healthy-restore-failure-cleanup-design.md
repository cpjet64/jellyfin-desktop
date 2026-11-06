# Diagnostic restore failure cleanup proposal

This proposal covers an armed B fixture when a malformed control or Grant frame,
endpoint loss, or another local protocol failure revokes restoration. It is a
design checkpoint, not a claim that the failure path is implemented or that the
fixture entrypoint is enabled.

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

On failure, the existing child owner immediately revokes the actual ChildGate
and the shared Player intent claim. If the Player was armed, the graph retains
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

The child owner must retain cleanup-only graph access after revocation rather
than immediately invalidating the graph before polling. The design needs
independent tests through the actual child, graph and Player boundaries for
failure during arm, load, seek, unpause and native callbacks; reentrant failure;
superseding intent; deadline expiry; and delayed or duplicate results. Tests
must show zero restore dispatches after revocation and no successful terminal
wire result for internal failure. Native execution, CI and production recovery
remain outside this checkpoint.
