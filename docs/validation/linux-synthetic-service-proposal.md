# Offline Linux synthetic-service proposal

This is an injected-backend design and local test candidate only. It has no
systemd transport, fixture executable, manager credentials, or native proof.
It does not create a unit, invoke a manager, send a signal, install software,
move a process, or authorize any package operation. The observed QGA service
cgroup remains shared infrastructure and is never a cleanup target.

For a unique 32-hex task, the candidate specifies a transient system service
`w644-<task>.service`, an isolated `/home/gitlab-runner/w644-<task>` workspace,
the fixed `/usr/bin/python3` executable and an isolated Python invocation of a
future synthetic fixture. The service properties are `Type=exec`, `User=999`,
`Group=988`, `Restart=no`, `Delegate=no`, `KillMode=control-group`, and
`SendSIGKILL=yes`. The environment and evidence path are fixed by the task
specification. There is no persistent unit file or delegated process migration.
The future fixture's reviewed bytes, interpreter identity, workspace ownership,
and evidence persistence must be independently pinned before any native run.

The manager adapter must create a transient unit with collision rejection and
return the exact start job identity. It must obtain and retain a unit reference,
then bind the exact unit, start job, invocation ID and dedicated
`/system.slice/w644-<task>.service` cgroup before any stop. It must preserve
the unit and process evidence before garbage collection, and confirm the exact
stop job and terminal result. It must also establish cgroup emptiness and absence
of descendants through independently qualified observation. A missing unit or a
client timeout is not proof of settlement or cancellation; a timed-out manager
request may still start later. Lost responses, inconsistent identities, missing
references, incomplete evidence and failed cleanup stop the candidate without an
automatic resubmission or another signal. The abstract adapter's statements are
not themselves proof that a real manager can provide these guarantees.

The candidate gives startup, runtime and stop separate finite limits under one
60-second outer deadline. An actual adapter must enforce each deadline across
its client and manager operations; the injected interface accepts deadlines but
cannot prevent a noncompliant backend or uninterruptible kernel operation from
overrunning them. Ambiguous timeout or cancellation leaves the outcome
unconfirmed and requires a separately reviewed recovery procedure.

Before any transferred workload, a separately authorized native synthetic
qualification must independently exercise ordinary completion, a stopped child,
a TERM-resistant child, and a session-changing child. It must demonstrate
dedicated ownership and bounded cleanup on success, failure and cancellation,
including late manager jobs, lost observations, surviving descendants and evidence
retention before unit collection. The present mocks do not establish those facts.
No APT or production suspension policy is proposed by this document.

The systemd manuals describe [`Type=exec` startup semantics](https://www.freedesktop.org/software/systemd/man/255/systemd.service.html),
[`KillMode=control-group` and `SendSIGKILL=`](https://www.freedesktop.org/software/systemd/man/255/systemd.kill.html),
and the [manager D-Bus unit and job interface](https://www.freedesktop.org/software/systemd/man/255/org.freedesktop.systemd1.html).
Those documented properties are design inputs, not evidence that this host's
manager will permit or safely contain the proposed operation.
