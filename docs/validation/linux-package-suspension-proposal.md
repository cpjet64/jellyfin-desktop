# Proposed package-process suspension on storage failure

This is a **source-only proposal**, not implemented behavior or authorization to
install a package, suspend a process, resume one, or alter a host. It addresses
the conflict between immediately stopping task writes on actual storage
exhaustion and avoiding termination of an in-progress package transaction.
The design and its native evidence must be reviewed before any behavior change.

## Scope and trigger

Consider suspension only after the approved transaction's package process has
started, when an operation on a task destination actually reports `ENOSPC` or
`EDQUOT`, including an equivalent verified wrapped error. A low-space estimate,
timeout, generic `OSError`, failed simulation, or non-storage error is not this
trigger and must not be silently reclassified. Before package mutation, preserve
the normal stop-on-failure behavior; do not start or continue a transaction after
storage exhaustion.

The process controller must run outside a dedicated, task-owned process group.
Before releasing the package command to mutate state, it records the exact
transaction, group leader, process-group/session identity and the durable
submission journal. The leader remains an owned, unreaped child while signals
may be needed. A terminal-but-unreaped child can be observed with `waitid` and
`WNOWAIT`; that observation is not a completed transaction. The exact package
process and its descendants must belong to the established group. If ownership,
namespace visibility or identity cannot be established, the controller must
not signal a guessed PID or a potentially reused group ID.

## Proposed response

1. On a verified storage error, stop the controller's own affected writes and
   prohibit dependent staging, receipts, cleanup, retries and new work. Keep the
   existing non-writing observation channel and known journal identity if they
   remain available. Do not try to create a new disk receipt on the exhausted
   destination.
2. While the original group identity is still held and verified, send `SIGSTOP`
   to that exact owned group. Never send a signal after ownership is missing or
   uncertain. Do not substitute a broad process-name match. A successful group
   signal only establishes delivery to at least one member; it is not proof that
   every member has stopped.
3. Within a finite diagnostic interval, enumerate the exact group in the verified
   process namespace and observe every live member in a stopped state. Preserve
   the unreaped leader and any package-manager locks. If the group disappears,
   a member escapes, observation is denied, a process remains in uninterruptible
   I/O, or the stopped-state check times out, report containment as **unproven**.
   Do not turn missing observation into a success receipt.
4. Report the storage failure and stopped or unproven state through the available
   non-writing channel. Keep monitoring without killing the package process or
   automatically resuming it. Capacity resolution, an explicit recovery decision
   and any subsequent continuation are separate actions; elapsed time is not
   permission to send `SIGCONT`. Do not delete or overwrite retained evidence.

If the observation channel is lost before or after a signal, preserve the last
known intent and PID journal and classify the outcome as unknown. Reconnect or
inspect only through an authorized read-only path that re-establishes identity;
never resubmit the package transaction, blindly re-signal or force cleanup based
on a timeout. If the controller itself cannot remain alive, document that its
unreaped-child guarantee and subsequent ownership must be re-established before
any later signal or recovery decision.

The controller must distinguish exhaustion on the guest's package, cache,
backup or evidence destination from exhaustion on the separate controlling
machine. A controller-only evidence-write failure stops that controller's own
writes. It justifies a guest-group signal only if a surviving authorized control
channel can still verify the exact live guest group and the applicable storage
condition; otherwise guest state is unknown and no signal may be guessed.

## Limits and evidence needed

`SIGSTOP` cannot retract a write already submitted to the kernel, and stopped
state is not a filesystem transaction or rollback. A process may create a new
session or otherwise escape the owned group; group-only observation cannot
prove containment of an escaped descendant. Package hooks, services, kernel
workers, other users and independent runner jobs are outside the owned group.
An idle observation is not a scheduler lease. The design must not claim those
actors were stopped or that a package database is consistent while suspended.

Before considering implementation, qualify the exact Linux process, signal and
namespace behavior, ownership and permission checks, group accounting, finite
observation deadline, controller-loss handling and durable journal transition.
Use controlled synthetic processes and injected error conditions, not a real
package mutation. Include actual `ENOSPC` and `EDQUOT`, non-storage errors,
in-flight writes, missing or exited leader, failed/partial signal delivery,
escaped descendants, observation loss and controller-volume failures. Verify
that locks and state remain held where observed, no unrelated process is
signalled, no automatic resume occurs, and unknown conditions never report a
successful suspension. Storage exhaustion still stops dependent writes even when
the process outcome remains unknown.

Linux documents that a negative PID targets a process group, and group-signal
success means at least one member was signalled, not that all were stopped:
[kill(2)](https://man7.org/linux/man-pages/man2/kill.2.html). The
[`waitid` documentation](https://man7.org/linux/man-pages/man2/waitid.2.html)
describes `WNOWAIT` and stopped-child observations. Neither reference establishes
the application's process-tree containment or guarantees a storage rollback.
