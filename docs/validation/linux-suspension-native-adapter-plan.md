# Standalone suspension adapter: qualification plan

The accompanying adapter is confined to synthetic fixture processes. This
source checkpoint does not authorize native execution, an apt/dpkg signal,
package installation, quota change, or a persistent monitoring service. The
portable tests use injected operations and mocked native decisions; they do not
prove Linux process, signal, or timeout behavior.

The fixture launcher creates a session leader and one child, retains the leader
without ordinary polling or reaping through the last possible group signal, and
creates a separate control process in another session. A finite registry records
start, parent, session and group identities for the two owned group members.
The controller serializes policy transitions. Bounded helper processes obtain
fresh snapshots, verify the finite registry against visible process ancestry and
group membership, and revalidate before a fixture-only stop or teardown signal.
Each returned observation has a new member dictionary. A helper timeout or
lost identity is unproven and cannot authorize a repeated stop.
An incomplete launch retains the known handles and identities in the raised
error, sends no cleanup signal, and cannot be retried on the same launcher.
Those records do not establish complete ownership: a new session can escape an
ordinary outer process group. Qualification requires containment that covers
such sessions and has a verified failure-path cleanup procedure before launch.
No established cross-session containment mechanism is identified in this
repository; its availability and authority remain unverified.

Before a native attempt, publish the implementation, tests, this plan and an
independent review. Obtain a separate execution decision and a dedicated process
lease. Use an owned outer containment mechanism and retain its complete outcome.
The native qualification must exercise and distinguish:

1. A verified `/dev/full` write produces real `ENOSPC`; `EDQUOT` remains an
   explicitly injected error unless a separately authorized isolated method
   exists. No shared filesystem is filled and no quota is changed.
2. The controller stays outside the owned group; the held leader and child
   preserve their recorded start/session/group identities. A stop request is
   followed by actual stopped-state observations for both, within the bounded
   deadline, while the separate control process remains unaffected.
3. Failed or partial signal delivery, escaped or inaccessible descendants,
   changed identity, a leader that exits before a possible signal, observation
   loss and helper timeout remain unproven. No prior numeric PID permits a
   repeated signal, and no automatic resume or resubmission occurs.
4. Explicit teardown signals only verified synthetic fixture identities. Verify
   group settlement before reaping the leader, then clean up the separate
   control process by its own verified identity. Unknown ownership or failed
   teardown is a failure requiring outer containment, never a success receipt.

The native helper's timeout must be validated under actual OS behavior; a Python
timeout alone does not prove an uninterruptible kernel operation can be stopped.
The stop helper receives the policy's absolute monotonic deadline and checks it
at the signal boundary. Launch steps share one deadline; process creation and
other kernel operations may still overrun it and require outer containment.
Teardown has its own single deadline, checked after waits and immediately before
the control-process signal. Expiry or ambiguous cleanup remains unproven.
Process-group checks do not establish containment of an undetected escaped
descendant, nor can `SIGSTOP` retract an in-flight write. No production recovery
or package-transaction suspension may be inferred from this fixture.
