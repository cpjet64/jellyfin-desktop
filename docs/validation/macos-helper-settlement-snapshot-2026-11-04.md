# macOS helper settlement snapshot

A single bounded, read-only snapshot was taken after the failed helper
acquisition. The preserved private terminal receipt has SHA-256
`1575c069b567eb3dab43abcd28685b36d787703e98b4f009a580fd21c8c1a3bc`.
The snapshot process exited successfully. Its exact-process and task-log
metadata commands each reported their own process group settled.

The exact lookup did not find the previously recorded acquisition process,
and the task-log opener check returned no match. A bounded runner-account
process inventory found no accessible Python, Git-family, shell, or Xcode
shim candidates, but it could not identify one process in the snapshot. The
original acquisition supervisor's start identity, process group, session,
and descendants were not recorded. The snapshot therefore reports the
earlier task's settlement as **uninspectable**; it does not establish that
all its processes are gone.

The snapshot's inventory itself runs beneath a held session leader. Source
review shows that the inventory can capture that leader while it exists, then
attempt executable identification after its group has been terminated. The
unidentified process is consistent with that sequence, but the receipt did
not retain the inventory leader's PID or the unidentified entry's parent,
group, session, and start fields. It cannot directly establish that the
unidentified process was the snapshot's own leader. Numeric PID ordering is
not identity proof.

The fixed guest-agent executable paths checked by this snapshot were absent;
that does not establish the guest-agent version or rule out another installed
location. The snapshot made no trust change, helper acquisition, helper
execution, retained-task cleanup, or signal to the earlier task.

## Bounded follow-up proposal

If further settlement evidence is authorized, take a fresh, independently
reviewed read-only inventory under the same exclusive runner lease. Record the
inventory's own held leader and command PIDs before enumeration, and preserve
the parent, group, session, start time and executable-identification result for
every ambiguous runner-account PID. Exclude a probe-owned process only when its
recorded identity matches, and report disappeared or inaccessible entries as
uncertain. Compare the remaining observed Python, Git-family and shell
processes with the known acquisition worker and its permitted subprocesses;
retain the exact command-group settlement evidence and raw private receipt.

This is a proposal, not authorization for another guest operation. The
observation is point-in-time and assumes the reviewed, non-malicious worker
and subprocess behavior; it does not claim proof against arbitrary process
renaming or session escape. No PID-only termination, lock removal, retained
file cleanup, trust change or acquisition retry follows from it.
