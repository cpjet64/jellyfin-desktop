# Offline Linux synthetic-service proposal

This is an injected-client design and local test candidate only. It does not
open a systemd connection and has no fixture executable, manager credentials,
installed-client proof, or native proof.
It does not create a unit, invoke a manager, send a signal, install software,
move a process, or authorize any package operation. The observed QGA service
cgroup remains shared infrastructure and is never a cleanup target.

For a unique 32-hex task, the candidate specifies a transient system service
`w644-<task>.service`, an isolated `/home/gitlab-runner/w644-<task>` workspace,
the fixed `/usr/bin/python3` executable and an isolated Python invocation of a
future synthetic fixture. The service properties are `Type=exec`, `User=999`,
`Group=988`, `Restart=no`, `Delegate=no`, `KillMode=control-group`, and
`SendSIGKILL=yes`. The environment assignments and evidence path are fixed by
the task specification. These assignments alone do not prove systemd supplies
no additional environment; exclusive environment remains a native qualification
requirement. There is no persistent unit file or delegated process migration.
The future fixture's reviewed bytes, interpreter identity, workspace ownership,
and evidence persistence must be independently pinned before any native run.

The proposed systemd 255 adapter must use one persistent **system bus**
connection, not the manager's direct private socket. Before submitting
`StartTransientUnit`, it must subscribe to manager signals and install its
job watcher. At transient creation it must request `AddRef=true` on that bus
connection, use collision-rejecting mode, and retain the connection and unit
reference through evidence persistence. The v255
[`systemd-run` source](https://github.com/systemd/systemd/blob/v255/src/run/run.c)
adds `AddRef` only for a bus client; the v255
[manager interface](https://github.com/systemd/systemd/blob/v255/man/org.freedesktop.systemd1.xml)
describes `Subscribe` and `JobRemoved` identities and results. Those sources
do not prove this host's authorization or a future adapter's behavior.

The adapter must correlate the returned start job with the subscribed terminal
job observation, then bind the exact unit, start job, invocation ID and dedicated
`/system.slice/w644-<task>.service` cgroup before any stop. It must preserve
the unit and process evidence before releasing the reference or permitting
collection, and correlate the returned stop job with its terminal observation.
Connection loss, late or missing replies, a collected job, and an absent unit
are unconfirmed rather than settlement. It must also establish cgroup emptiness and absence
of descendants through independently qualified observation. A missing unit or a
client timeout is not proof of settlement or cancellation; a timed-out manager
request may still start later. Lost responses, inconsistent identities, missing
references, incomplete evidence and failed cleanup stop the candidate without an
automatic resubmission or another signal. The abstract adapter's statements are
not themselves proof that a real manager can provide these guarantees.

The offline binding targets the published
[`dbus-next` 0.2.3 asyncio `MessageBus` API](https://python-dbus-next.readthedocs.io/en/latest/message-bus/aio-message-bus.html).
An externally created, connected system-bus `MessageBus`, its owning idle loop
and thread, the library's `Message`, `Variant`, and `MessageType`, and a fixture
observer are injected. It uses `add_message_handler` and
`await MessageBus.call(Message(...))` with the absolute deadline, and rejects
a running or foreign loop. It opens no bus, installs no library and provides no
nested-loop fallback. System-bus scope is a preverified caller assertion, not
proved by the boolean; client availability and native behavior remain unproven.

The binding uses D-Bus `GetNameOwner` and `AddMatch`, then manager `Subscribe`,
and buffers matching `JobRemoved` signals before `StartTransientUnit` with
signature `ssa(sv)a(sa(sv))`, mode `fail` and typed variants. It pins manager
calls to the verified unique owner. It maps `GetUnit`, `Properties.GetAll`
for `Unit` and `Service`, `StopUnit`, and `UnrefUnit` after saved settlement
evidence. A returned job is retained before post-reply validation; job signals
are correlated even if received before the method reply. The bounded job queue
fails closed on overflow and ignores unrelated units.

The fixture observer is a separate unimplemented native component. Its
name-filtered watch must be installed before creation without loading the unit;
its cursor is captured before an evidence snapshot, and its next-change call
must return a strictly later cursor or fail by the unchanged deadline. It must
retain changes between snapshot and wait. Owner and connection loss, missing
evidence or an absent unit remain unconfirmed. The caller must retain the
adapter, loop, bus and observer as one owner after an uncertain result. `AddRef`
is connection-scoped: discarding that owner or closing the bus can release it;
a recorded reference flag is not lifetime proof. No automatic disconnect,
unref or retry occurs on uncertainty.

The observer independently obtains current invocation, cgroup, connection and
reference state, and case-specific fixture evidence; it is not given the expected
identity to echo. Each capture supplies its watch cursor and a strictly advancing
observation revision. Captures immediately before stop and release must match the
bound identity and case; release also requires unchanged settlement evidence.
These checks rely on a future independently qualified observer and are not an
atomic check-and-stop operation. The injected fake demonstrates rejection paths,
not native observation, reference ownership, or containment.

The offline real-type test requires the official `dbus-next` 0.2.3 wheel, size
57,885 bytes, SHA-256 `58948f9aff9db08316734c0be2a120f6dc502124d9642f55e90ac82ffb16a18b`,
provided in a job-local file. The gate provisions the fixed artifact before the
test; it verifies bytes before zipimport and fails when unavailable or mismatched.
The test does not install the package or open a bus. Provisioning is a separate
network step and has not been exercised in this offline qualification.
It requires curl 8.4.0 or newer for its streaming size limit and fails closed
on older or unrecognized versions; actual runner versions remain unverified.

Ordinary mode waits for actual fixture completion and a passing fixture result;
an active unit alone is not completion. Stopped-child, TERM-resistant, and
session-changing modes wait for a matching-mode fixture-ready observation before
an explicitly authorized stop. The same identity and retained reference are
checked at every observation; a settled failed fixture is not a pass. Uncertain
outcomes retain the last known task, unit, submitted job and binding without an
automatic resubmission or repeated stop.

The candidate gives startup, runtime and stop separate finite limits under one
60-second outer deadline. Manager start, runtime and stop limits are 10, 24 and
8 seconds, mapped to `TimeoutStartUSec`, `RuntimeMaxUSec` and `TimeoutStopUSec`
as unsigned 64-bit microsecond variants. Client phase ceilings are 12, 25 and
15 seconds, each capped by the outer deadline. The outer clock starts before
setup; client start begins before submission, client runtime after the observed
start result, and client stop before its stop request. The manager timers begin
at their respective service lifecycle events, not necessarily those client
points. The stop ceiling reserves time after the manager timeout for terminal
notification and evidence; nominal durations alone do not prove an overall bound.
An actual adapter must enforce each deadline across
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
