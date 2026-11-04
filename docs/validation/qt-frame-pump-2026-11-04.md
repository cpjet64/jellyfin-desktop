# Qt frame-pump pause and resume: Windows validation

The standalone Qt-only fixture at source revision
`45cf1169e51fb4c4de097e06fce0d3bbe4945910` passed on Windows with Qt 6.9.3
and MSVC 2022. Its Debug and Release runs each recorded 12 passed, zero failed,
and zero skipped cases. This is fixture evidence, not a product release result.

| Build | Test executable SHA-256 | QTest result |
| --- | --- | --- |
| Debug | `873b48556c7677b1b602a76e29739c1ab61db3c96eff189e3316787ead890485` | 12 passed, 0 failed, 0 skipped; 2589 ms |
| Release | `808cd052efeb777c8d238bdfd7d175d4b1ab1e214b3a9206dff64c5ae1b50b9c` | 12 passed, 0 failed, 0 skipped; 2565 ms |

The table records a rerun of the same binaries with retained QTest and desktop
helper logs. In each run the helper reported a known natural application exit
of zero, known zero active job processes before and after cleanup, and no
termination request or forced cleanup. Both helper stderr logs were empty.

The pause/resume case checks that the render worker acknowledges the pause
with its expected context, has no frame in progress, and starts no further
frames during a 250 ms observation. It then checks resumption on the same worker
and context, further rendering, and orderly shutdown while paused. The separate
independence case checks distinct bright frames while GUI event processing is
withheld for approximately 350 ms. Failure-injection, pending helper-readback
settlement and blocked-cleanup cases also passed. The pending-readback case
cancels an outstanding helper request while its completion is withheld; it does
not test cancellation of an asynchronous GPU operation. Wrong-private-state is
a synthetic guard rejection, not a test of a corrupted Qt object.

These results do not establish native Linux or macOS behavior, production
recovery, or libmpv playback. The healthy-mpv rerun is separate and was pending
when this report was prepared. The saved QTest logs and built executables remain
local validation evidence; no new package or release is asserted here.
