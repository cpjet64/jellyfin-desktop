# B cancellation identity gap

The approved cancellation contract requires a terminal result bound to the B
run, launch, grant, revision, cancel sequence, restore attempt, and core
generation. A control acknowledgment means only that cancellation was received;
it is neither native cleanup nor renderer and process closure.

The current `GrantAccepted` frame binds run, launch, grant, revision, and the
canonical grant digest. It does not carry the actual Player restore attempt or
core generation. The parent therefore cannot validate those two fields in a
later terminal result against an authenticated retained receipt today.

For an armed Player, one candidate is a versioned acceptance receipt containing
the actual attempt and core identities established by that arm. The parent
would retain the receipt from the owned child channel and require an exact match
in the distinct terminal result. A successful result would remain separate from
the subsequent renderer, worker, core, and process closure evidence.

Cancellation before the grant is accepted or a Player is armed has no such
attempt or core identity. Missing identities must not be fabricated, and absence
of an arm receipt alone must not establish `NoLoad`. Possible contracts include
an explicitly defined `NoArm` variant with absent attempt and core fields and
independent owned-launch evidence, or deferring the terminal result until an
appropriate authenticated identity exists. Neither alternative is selected here:
its ordering, failure handling, and evidence requirements need a decision.

Until that decision and implementation, an unqualified or ambiguous terminal
result remains unconfirmed and follows bounded containment. A user cancellation
cannot pass the restoration demonstration even if cleanup later succeeds.
