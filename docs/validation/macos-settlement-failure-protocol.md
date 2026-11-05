# Settlement failure-record protocol

The W511 terminal receipt established that the corrected snapshot failed, but
the nested wrapper discarded the phase and category before the parent raised a
generic error. Its historical process settlement remains uninspectable. This
protocol is an offline, transport-neutral correction for a future reviewed
adapter; it does not run a process, inspect a guest, or alter the original
receipt.

`dev/ci/settlement_failure.py` accepts injected spawn, nested operation,
cleanup and protected-channel emission functions. A caller must register each
owned group immediately after it is created, including on a partially failed
launch. The nested operation records already-observed command and identity
facts and classifies failures without collecting new process information.
The parent retains that nested record, attempts cleanup for registered groups,
and emits the bounded record before propagating failure.
The cleanup obligation is established before spawn and remains in a finalizer
for registered groups. Its unconfirmed defaults cannot become a success.
The private adapter attempts settlement, each outstanding descriptor close,
and restoration of a known prior signal mask independently. A failed signal
block is recorded as the fixed detail `mask_block_failed`; it cannot pass.
These guarantees cover the tested catchable failure paths, not arbitrary
asynchronous interruptions or hard termination.

For each group the cleanup adapter reports whether cleanup was requested, its
operation result, leader reap and independently observed group settlement.
Unknown, not attempted, disappeared and mismatch states are distinct. An
unrequested or unproven group settlement is failure even if the inventory
operation otherwise succeeded. A primary failure remains primary when cleanup
also fails; secondary cleanup uncertainty is separate. An unwritable response
channel or hard termination can still leave no durable evidence.

The record accepts only fixed role, phase, category and numeric observation
fields. It bounds groups, events and serialized size; overflow cannot become a
pass. It performs no disk write and carries no command lines, environment
values or arbitrary exception text. The caller remains responsible for the
existing process identity, output/deadline limits, first-terminal collector,
protected evidence directory and storage-exhaustion behavior. Private process
identity observations must remain in protected evidence.

The portable injected tests exercise the nested-to-parent protocol,
including primary and secondary failures, pre-spawn and partial-launch paths,
unknown cleanup, identity observations, output limits and channel failure.
The corresponding private wrapper draft connects the held-leader frame and
parent cleanup to this helper; its separate injected tests exercise that actual
boundary. It and its launcher require independent review and exact source pins.
Native behavior and any new snapshot require a future execution decision. No
retry, trust diagnostic, acquisition, lock removal or guest work is authorized
by this document.
