# Recovery boundaries checkpoint — 2026-11-05

This checkpoint records local evidence for source revision
`fc7f18b32618867dbcf47ef22b251ad2dcba1fdf`. It does not establish completed
product recovery or native qualification.

The integrated changes are:

- `70b4876ea9d6389be79fcaccbd434d3d069f4ec7`: reject ambiguous settlement
  observation claims and bind observations to the attempt.
- `065a2ece93f1edf33a590c817c86ef66747a0655`: split the fixture event
  consumer handoff and update its isolated producer copy.
- `fc7f18b32618867dbcf47ef22b251ad2dcba1fdf`: qualify the offline synthetic
  service D-Bus boundary using pinned library message types and a fake transport.

## Local evidence

At the exact checkpoint revision, the Windows Release, Debug and fixture builds
exited successfully. Release passed 27 selected tests, Debug passed 3 tests,
and fixture-copy checks passed 6 tests. The fixture dependency graph review
covered 48 relevant object records: 13 selected the copied player header and
none selected its original. All four corrected translation units selected the
copied header; generated MOC inputs and isolated fixture link were also checked.

Windows-hosted offline tests passed 40 Linux synthetic-service tests, 12 native
order tests, and 54 settlement tests; the Windows smoke gate passed. These tests
use mocks or injected transports where applicable and do not demonstrate native
Linux or macOS execution. Additional private injected wrapper and poll tests
passed 53 and 15 cases respectively; they did not submit concurrent live work.

The held fixture was not executed. Native callback barriers, render retirement,
destruction, and retention of ownership during uncertain outcomes remain
unqualified. No guest or CI execution is claimed by this checkpoint. The Linux
native observer and real system-bus behavior remain unqualified, as do the
corresponding native macOS recovery and settlement behavior.
