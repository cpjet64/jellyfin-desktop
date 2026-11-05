# Linux containment capability inspection: review proposal

This is a preparation-only collector. It does not authorize execution, a
transient unit, a fixture, or host changes. No capability is established by
this source. The coordinator must independently review the exact collector and
private submission adapter, obtain a fresh lease and target/identity checks,
and approve the single bounded inspection before any guest execution.

On the separately verified intended guest, collect two explicitly pinned
contexts: the actual guest-agent controller identity and the intended runner
identity. The submission adapter must run each fixed collector role under the
corresponding existing identity, never create a session or user bus, bind both
reports to one task and verified guest, and preserve failures as evidence. The
collector arguments are `controller 0` and `runner <verified-nonzero-uid>`;
the adapter must compare report hostname, account and UID against independently
verified expectations, and fail closed on ambiguity. The role arguments are
not themselves identity proof.

For the separately approved follow-up task, the expected guest hostname is
`runner`, as observed by the byte-exact preflight. The VM routing identity
remains VMID 201 with the inventory name `ubuntu-runner-201`; routing and the
runner account and UID must be independently checked. Both controller and
inline-drop reports must identify hostname `runner` and their expected role,
account and UID. This task-specific expectation does not alter the earlier
failed report or establish future host identity.

The candidate driver inspects the QGA controller process and then permanently
drops that same process to the runner account using initgroups/setgid/setuid.
Label this as an inline-drop context: it is not an observation of an existing
runner-service process, runuser/PAM, or a future fixture's context. Seek
independent review on whether this evidence is useful and sufficient before
execution. Any later fixture route must match the observed context, or obtain
separately authorized evidence for its actual context.

The collector reads selected cgroup v2 mount and membership, ancestors' owner,
permissions, ACL, controllers, subtree control, events and cgroup.kill access;
it records identity, process/session, kernel, PID 1, systemd version and fixed
unit/slice properties. Fixed system and user manager queries retain failures;
the user query uses the existing context only. It does not read environment
contents, create units, change delegation or cgroups, or signal fixtures.
The `gitlab-runner.service` and `user-<uid>.slice` queries are reference
observations, not identification of the QGA process's owning unit. Its owning
unit and delegation remain unknown unless mapped from observed cgroup evidence.
Commands are fixed to systemd --version, getfacl on the observed ancestor
paths, and systemctl show for selected properties. Read and report limits,
per-command two-second bounds, overall alarm, child cleanup, and fail-closed
parsing limit the work; kernel stalls remain an external limitation.
Cleanup begins under the same guard as child acquisition, but an unreturning
Popen or interruption between Python bytecodes cannot establish absolute
cleanup immunity. Cleanup uncertainty stops further queries and the inline
identity drop; ambiguous child settlement remains unproven.

Before submission, a fresh read-only cluster placement check must establish
the exact target and name; no automatic fallback is permitted. Pin the
collector, adapter, identities and lease, journal intent before one submission,
record the guest-agent PID and poll that exact PID with finite bounds. An
unknown launch outcome forbids resubmission. The result only describes the
observed contexts: access bits, manager reachability, and unit properties alone
do not prove authorization or containment. Any later resource creation or
native qualification needs a distinct reviewed decision and authority.
