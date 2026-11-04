# Fixture-only Linux suspension qualification

The accompanying portable policy is a synthetic decision fixture. Its injected
backend supplies observations and records a requested signal; it has no native
process adapter, package-manager wiring, persistent monitor or installation
authority. A signal request, an observed stopped group, and transaction
completion remain separate results. No native signal behavior has been tested
by these policy tests.

The fixture admits only an explicitly owned group whose leader, controller and
lease identities pass validation. It requires non-reaping observations, complete
descendant visibility and no escape before requesting a single stop. It then
requires matching retained identities and all observed members stopped within
a finite sample/deadline bound. Failed delivery, partial stopping, a reaped
leader, changed member identity, escaped or inaccessible descendants, timeout
and observation loss are unproven. The one-use policy never retries a signal,
resumes a group or authorizes resubmission. Controller-volume exhaustion is a
separate result and does not become a guest storage error. These are injected
contract observations, not proof that an operating system supplies them.
The synchronous policy cannot interrupt an injected backend call that never
returns. A future native adapter must independently bound each observation and
signal call and qualify its watchdog behavior; the sample deadline alone does
not provide that containment.

The portable test file must be registered and run on Windows, Linux and macOS;
passing on one platform is not native cross-platform evidence. After this
implementation, plan and independent source review are published, seek a
separate native execution decision. A native Linux qualification should use
only newly created fixture-owned processes and a controller outside the group:

1. Record exact parent, session, group, member and unrelated-control identities.
   Verify the group leader is observed without reaping until the final possible
   signal, and reject special, foreign or controller targets.
2. Use an existing verified `/dev/full` for a real `ENOSPC`; inject `EDQUOT`
   unless a separately authorized isolated mechanism exists. Never fill a
   shared filesystem or modify quotas. Test controller and guest origins
   separately, and retain timestamps and actual stopped-state observations.
3. Inject partial/failed signal delivery, observation loss, inaccessible or
   escaped members, leader exit and a bounded wait that cannot establish all
   stopped states. Those cases must remain unproven, with no stale-PID signal,
   automatic resume or resubmission. Include a separate unrelated process and
   prove it is untouched.
4. Make fixture teardown explicit and scoped to verified fixture-owned
   processes. Retain the complete process outcome and prove no owned process is
   left behind; an uncertain identity or failed teardown is a failed or unknown
   result, never a pass. Synthetic teardown does not authorize terminating a
   live package transaction.

`SIGSTOP` does not retract an in-flight kernel write, and process-group evidence
cannot by itself prove containment of escaped sessions. Live apt/dpkg integration,
automatic suspension or resume, a package installation and any production
storage recovery remain outside this slice.
