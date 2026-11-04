# Corrected macOS settlement snapshot: failed attempt

The single approved corrected read-only settlement snapshot was attempted on
2026-11-04. Its reviewed guest source SHA-256 was
`bd71493887cea228dd08d2080873e661e32bc91a08ad54f73d5771036765e789`;
its client SHA-256 was
`cf36ab1387cf53a5edd6bd44d2aa5c8b4b3d27524a55ee220aedf5c3b4a28d26`.
The first complete private terminal response was preserved at 12:39:37 UTC;
its SHA-256 is
`5c7270637510e6604c235111cec24001c07b9d19658af2665dfd92914cb18cbd`.

The snapshot process exited with code 1, recorded stderr, and emitted no
inventory report. The bounded traceback reaches the inventory operation and
reports a generic metadata-command failure or bounds error. The held-leader
wrapper reduced its nested failure to an exception type before the caller
raised that generic error. The original triggering condition is therefore
**not established** by this receipt.

The source's control flow returns from the inventory command's cleanup before
the generic error is raised. That supports a **source-based inference** about
the snapshot's own command-group cleanup, but the failed run emitted no
explicit settlement receipt for those groups. The result does not establish
current quiescence. The earlier acquisition's historical settlement and the
previous snapshot's unidentified process remain uninspectable. The
coordinator retains the exclusive runner lease pending a separate decision.

No second snapshot, trust diagnostic, helper acquisition or execution, signal
to the earlier task, lock removal, or retained-file cleanup was performed as a
result of this failure. Private raw terminal evidence remains preserved.

## Offline diagnostic proposal

The reviewed source can produce the same outer error for several different
conditions, including command timeout or output bounds, unavailable process
identity, malformed inventory, and an own-process identity mismatch. Source
inspection alone cannot determine which occurred here.

For a future decision, prepare a bounded structured failure record that retains
the failing phase and an allowlisted nested error category before raising, and
separately records whether each probe-owned group was actually settled. An
unknown or failed cleanup must remain unknown or failed, never a pass. Preserve
the complete first terminal response and avoid exposing command lines,
environment values, or private process data in public reports. Such a change
would require tests and independent review; it does not authorize another
snapshot or any other guest action.
