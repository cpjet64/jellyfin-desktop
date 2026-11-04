# Windows hidden-desktop helper validation — 2026-11-04

The helper and fixtures were built and tested at [222c394d6a0399c5caafdba265e2aca0cb7f2670](https://gitlab.curtpme.com/cpjet64/jellyfin-desktop/-/commit/222c394d6a0399c5caafdba265e2aca0cb7f2670) using native MSVC 2022 14.44 with `/W4 /WX`. Source identity was unchanged through the run. Both standalone outcome-policy and injected Win32 API-error tests exited zero.

All four real-helper cases produced their required outcomes:

| Case | Process wait / exit | Known active count before cleanup | Explicit termination request | Known final count | Helper outcome |
| --- | --- | --- | --- | --- | --- |
| Natural success | Signaled / 0 | 0 | No | 0 | Pass |
| Natural failure | Signaled / 42 | 0 | No | 0 | Fail |
| Deadline exceeded | Timeout / 259 (`STILL_ACTIVE`) | 1 | Yes; API succeeded | 0 | Fail |
| Root exits with descendant | Signaled / 0; child readiness confirmed | 2 | Yes; API succeeded | 0 | Fail |

All exit and accounting queries succeeded. Cleanup did not turn any failing case into success. A successful termination request does not by itself prove that a live process was killed; the recorded API results and before/after counts are the evidence. The legacy `forcedCleanup` marker now records whether explicit termination was requested.

| Executable | SHA-256 |
| --- | --- |
| Hidden-desktop helper | `706e0a939b4dff4b2f2996996863943c3d3a5dfba6dc46e96578f4fb73bd7a42` |
| Native child fixture | `c09a102ff794adac62fca84937d0dfb3a8f1ffc4b24cf95c4f7611e370d5a9ae` |
| Outcome-policy test | `4d7f226a5acc5579046df415f0ce0ecddfa5c3dc7553e95aaf497a0d37bbb797` |
| Win32 API-error test | `539539b76fbf05e65ac2a5159b9cc07ca4061848968a8953802c10a227efcadc` |

The integrated portable CI suite passed 86 tests, and the Node helper-log regression passed. These checks reject natural nonzero exit with no termination and an empty job, unknown results, and conflicting success records. Linux/macOS receipt paths remain separate and their native obligations are outstanding.

This is helper validation, not application playback or recovery proof. The earlier failed mpv experiments and their original helper records remain unchanged. No product package was rebuilt and no pipeline was triggered for this check.
