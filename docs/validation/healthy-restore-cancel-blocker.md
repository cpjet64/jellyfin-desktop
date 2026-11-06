# Healthy B cancellation: synchronous cleanup blocker

Status: source finding for review, not authorization to run the fixture. The
healthy diagnostic graph/session remains incomplete and native execution is held.
The B child reducer and adapter inspected here were the W936 worktree at
`c9391c450785a5248834ecf29aca4d2a6607f15f` with its in-progress local edits;
the evidence is a candidate source path, not a landed integrated build.

## Observed path

The candidate `tests/fixture_restart/RestartChild.cpp:57-94` processes ordered
control and invokes its cancellation action when a valid newer Stop/Play or a
protocol failure revokes restoration. `RestartChildPlayer.cpp:38` forwards
`revoke()` to that reducer. The action uses the same `ChildGate` through
`FixtureRestartAdapter::cancel()` (`FixtureRestartAdapter.cpp:73-85`), so a
revocation prevents future authorized restore dispatches.

If the B adapter is armed, its checked-stop callback calls the actual Player's
`cancelNativeRestore()`. In `src/player/PlayerNativeRestore.cpp:103-116`, that
method records cancellation, validates the current restore key, invalidates the
native receipt and changes playback intent. If load was attempted, it then
calls `stopMpv()` synchronously. `src/player/PlayerComponentCommands.cpp:93-105`
uses the injected stop operation when supplied, otherwise calls `mpv_command`
directly on the current mpv handle. The checked callback treats a native
accepted result as its success condition after a load attempt.

This path executes on the Player/control thread; it can block that GUI thread in
the synchronous native command. A returned acceptance also is not evidence of
the later matching command reply and terminal playback state. The no-load case
does not need a native stop, but must remain revoked. The W902 asynchronous Stop
is currently scoped to A's internal replacement operation; it does not by
itself replace this B cancellation path.

## Candidate for review

Use the existing B Player and its single externally owned event consumer to
submit a one-shot, uniquely tagged asynchronous cleanup Stop only when B
actually attempted a load. Revoke the existing shared Gate/intent authority
*before* submission so no later load, seek or unpause can dispatch. Preserve the
actual owned B restore key, request identity and known playlist-entry evidence;
observe the matching reply and required terminal/idle evidence asynchronously.
For a proven no-load/no-entry case, complete the applicable no-native-stop
branch without inventing an END_FILE. Failure, cancellation, deadline expiry,
unknown entry, lost worker or missing reply remains incomplete and goes to the
existing outer process containment; it is not a cleanup success.

The current synchronous `bool` cancellation/ACK path would need an explicit
pending/completed result and must not ACK completed cleanup merely because the
command was queued. Reuse the original overall deadline and existing channels;
do not add another event consumer, watchdog, native core or production Stop
change. Confirm the exact B control-ACK contract and no-entry criteria before
implementation. This candidate does not authorize code changes or a native run.
