# Concurrent macOS metadata observation: proposed contract

This is an offline design proposal, not authorization to execute an observation.
The existing conditional allocation remains held. A replacement requires a
separate decision after review.

## Claim and boundaries

One attempt may collect bounded process metadata and bounded identity diagnostics
for that attempt. Unrelated processes may start, exit, or change during collection.
The result describes only the data actually collected, with uncertainty explicit;
it does not establish a consistent whole-machine snapshot, complete process
visibility, process absence, idleness, settlement, historical cleanup, or grounds
to release a lease. A failed operation remains failed even if diagnostics exist.

Each attempt has one owner, distinct private working state and one fresh protected
receipt. Concurrent attempts must not share mutable state, consume one another's
output, or contend for ownership of a process handle. The design must establish
ownership across the entire lifecycle, including ambiguous interruption, rather
than infer it from a filename alone. No retry follows a failed, timed-out, or
ambiguous attempt under the same allocation.

Collection, records, diagnostics, finalization, and cleanup retain finite bounds.
Partial, malformed, unavailable, truncated, or ambiguous evidence is unknown or
failed as appropriate; it must never be presented as complete. Preserve the first
failure and cleanup uncertainty. Do not widen the existing collection commands.
No process observed in inventory is signaled, waited for, or otherwise acted on
because of its observed identity. Cleanup is confined to helpers actually launched
and controlled by this attempt; uncertain ownership or cleanup remains uncertain.

## Required evidence before any replacement execution

| Case | Required proof and present limitation |
| --- | --- |
| Unrelated churn | Disappearance or changed identity of an unrelated process cannot invalidate an otherwise attributable observation; that row remains uncertain. The current private draft can instead fail overall qualification. |
| Same name or account | Unrelated rows cannot become owned helpers or authorize action. The public candidate helper's unrelated-row redaction test covers only its diagnostic output, not the entire observer. |
| Identity changes | Changed or unavailable identity cannot be asserted as a match. Prove the distinction between uncertainty in an unrelated row and failure of an owned helper's identity. |
| Overlapping collectors | Demonstrate distinct inputs, working state, handles, and receipts, plus one owner for each attempt through interruption and cleanup. Current source does not establish that lifecycle isolation. |
| Partial output | Demonstrate finite limits and explicit incomplete or failed outcomes without converting partial data into absence. Public candidate tests cover malformed input and diagnostic bounds, not complete end-to-end collection. |
| Cleanup uncertainty | Demonstrate only owned helpers are targeted, first failure survives, and uncertain cleanup stays uncertain. This does not prove cleanup of earlier attempts or unrelated processes. |

The existing public diagnostic tests in `dev/ci/test_settlement_candidates.py`
provide bounded parser and redaction evidence only. The private draft currently
strictly parses inventory, caps examined rows, and records ambiguous unrelated
identities as qualification failures. Those behaviors do not establish the
proposed churn-tolerant claim. Its existing supervisor cleanup and one-shot
journal are not proof of concurrent lifecycle isolation. Design and review must
resolve these gaps before any execution decision; this document changes no code,
process structure, command, or authority.
