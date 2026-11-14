# Fixture copy patch proposals

These source-only proposals apply to assembly parent `adbf0fe99c991246b889d01b1c0b7b65a64280df`; the historical fixture patch was based on `4453818fcdeae3152fd07f29031ec6f3eb3b1670`. They do not update input, output, or patch hash assertions and do not qualify any executable gate.

| Mode | Proposed fixture event behavior |
| --- | --- |
| External events OFF, recovery runtime OFF | Preserve the historical fixture owner `beginHandoff`, `finishHandoff`, and `postHook` calls and failure handling. |
| External events ON, recovery runtime OFF | Fixture preflight rejects this mode; the historical fixture owner path is retained in the proposal. |
| Recovery runtime ON | The patched setup returns true to preserve the current no-op and continuation; runtime finish and delayed-hook branches are unchanged. Fixture preflight does not authorize this mode. |

The current production fixture receipt, observation, and seek sources retain their separate native userdata allocation and logical IDs. The old unused `wakePlayer` helper has no remaining call in the proposed event postimage; the fixture owner installs and hands off its own callback. The fixture test's existing `QFAIL` hold remains present. The video item guard, owner field, and readiness assignment were relocated to their intended current-source positions after a zero-context application initially misplaced them.

The isolated postimage at `deps/fixture-postimage-rcn233` is for source review only. Its `SHA256SUMS.txt` records raw bytes; it contains extra player files and is not a claim that it exactly matches the configured copy set or its LF-normalized hash process. The first fixture patch was applied and the second was applied to this postimage; the complete zero-context patch, all resulting members and functions, other diagnostic chains, copied-source lists, and pins still require independent review. The separate healthy event-order patch adjustment changes context only; its complete patch chain remains unchecked.
