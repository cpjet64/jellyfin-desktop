# Healthy restart and resume: diagnostic plan

This is a proposed native acceptance scenario, not an implemented or executed
test. It covers a deliberate healthy process replacement using a test-owned
local asset. It does not establish recovery from graphics loss, a crash, or an
authenticated Jellyfin session. Existing native fixture holds remain in force.

## Setup and identity

Use the existing desktop runner's owned-root checks, isolated profile and
new-only evidence directory, finite deadlines, held runner/process lease and
platform-specific process containment described in
[the synchronous dispatch plan](synchronous-dispatch-native-plan.md). Generate
a deterministic, changing-frame fixture with `tests/generate_recovery_media.py`.
Record its hash and the actual path and playlist entry observed by the player;
two file names or hashes alone do not prove different frames. Record source and
build identity, platform, monotonic timestamps and the exact owned process and
child identities as established by the runner. Never identify a process merely
by its name or signal an unowned process.

Proposed fixed budgets are 180 seconds overall, with 30 seconds each for A
startup and playback, checkpoint capture, A exit and settlement, B startup and
consumption, and B observation and settlement. These five phases total 150
seconds, reserving 30 seconds for remaining cleanup within the overall limit.
Each phase, including cleanup, uses the earlier of its own deadline and the
original overall deadline; unused time does not reset
either deadline. Use the existing generated 10-second clip (300 frames at
30 fps) and capture A at a validated position between 2.9 and 3.1 seconds.
Transfer the actual checkpoint position, not a fixed three seconds. Unexpected
EOF fails the case; do not loop or reload. Before unpause, B must observe its
restored position within 100 ms of the checkpoint and then, for a playing case,
require at
least one second of advancement within ten seconds. The paused case requires
position to remain within 250 ms for three seconds before explicit resume.
For each changing-frame assertion, capture two decoded output frames at least
one second apart and compare their pixel content; identical frames fail.

Process A must load the fixture, show advancing position and two distinct
visible output frames. Capture a bounded, validated checkpoint tied to A's
current logical intent, asset identity, position and playing or paused state.
The existing `PlaybackCheckpoint` codec is conservative: it does not authorize
automatic resume. A future diagnostic transfer must be explicitly scoped to
this test, validate the checkpoint and bind it to the fixture; the checkpoint
must not contain a URL, credential or reusable server session. Treat missing,
ambiguous, stale, malformed or incomplete checkpoint evidence as failure.

## Replacement and consumption

Issue an explicit diagnostic restart while preserving that intent. Use the
existing runner's process containment and lifecycle route, without depending on
the held C2 fixture or an injected same-core cleanup coordinator. Record the
executable hash, PID, process creation time and held process handle for A, plus
the runner's owned-child identity evidence. Require observed operating-system
exit and settlement of A and all owned children before spawning B. Forced
cleanup is a failed scenario even if it eventually settles the process tree.
Preserve the
original failure and any cleanup failure separately. A timeout, uncertain exit
or incomplete settlement fails the scenario and prevents B from spawning.
On failure, terminate only descendants in the task-owned job or equivalent
platform allocation, under its established cleanup rules.

Launch genuinely new process B under the same bounded containment, with a new
process identity, profile and evidence namespace; record B's executable hash,
PID, creation time and held handle. The proposed transfer is a parent-owned
inherited pipe separate from logs. Its single framed record contains at most
the codec's 8 KiB checkpoint limit plus an envelope capped at 1024 bytes: the
complete record, including framing, is at most 9216 bytes. The envelope has a
fresh run and attempt nonce, A's process identity and fixture asset hash. The
parent validates the source and burns the one-use record before launching B or
transferring it. Authority comes from the owned inherited channel and its
independently bound endpoints, not from JSON fields; B validates the envelope
and codec before consuming it. EOF, extra records,
wrong bindings, partial input and timeout fail closed. B resolves the local
fixture afresh and records its own observed path and playlist entry. B starts
paused at the beginning without a checkpoint offset. Capture B's actual output
with a valid baseline counter index 0–2, then arm the seek and retain bounded
captures associated with that operation. Validate the matching entry, seek reply,
subsequent playback restart, a fresh native position observation within 100 ms,
and a target capture whose decoded counter time is within 100 ms of the actual
checkpoint and differs from the baseline. The target capture may precede delivery
of the matching acknowledgements; retain its original operation and timing, and
correlate it only once all required facts arrive. Never retag an earlier image.
Only then apply the requested playing or paused state. A snapshot position alone
does not establish seek completion. Pending
Stop or replacement intent burns permission before any play or unpause; its
acknowledgement must precede release of any blocked continuation. Internal
cleanup is recorded separately and must not masquerade as user Stop. B must
create and bind its own host, core, intent and entry identities, not rewrite A's
identifiers. B must not reuse A's process, player, core,
render context, or stale callback as evidence of a fresh start. This scenario
does not require proving retirement of a particular graphics context or core.

Render update flags and render serials do not independently prove a new decoded
frame. In the fixture-only copied renderer, on the owning render thread with
the original GL context current, record the update and next-frame-info results
and raw flags, perform the existing render, and read back its target before
another render overwrites it. Publish an immutable image and record with the
render-pass serial, expected operation key, thread/context, result and timing.
The key is the fixture's expected ownership, not an image identity returned by
mpv. Continue normal rendering even when a pass cannot qualify; callbacks remain
wakeups and no new event consumer or advanced render control is introduced.
Bind each captured render target to B's process, expected core/entry and
armed operation, and validate geometry, orientation, counter blocks and moving
square; reject ambiguous, blank, flat or stale images. The baseline-to-target
transition establishes newly presented B output, not the decoder's exact
production time. Synthetic images test acceptance logic only.

For a playing checkpoint, require B's decoded output to show two distinct
frames and position to advance from the restored position within a documented
finite tolerance. For a paused checkpoint, require the restored position and
paused state to remain stable over a bounded interval; explicitly resume and
then require advancing position and distinct decoded frames. Capture evidence
from B itself, with timestamps and process/entry binding; a command return,
promise, static screenshot or queue entry alone is insufficient.

## Negative cases and evidence

Run Stop and replacement-intent cases separately in fresh processes. Deliver
each before checkpoint consumption and prove that the old checkpoint cannot
start playback. Deliver a delayed A-origin message after B starts and show it
cannot change B's intent, playback or checkpoint consumption. Record the
message origin, the relevant consumption and native command boundaries, and a
bounded observation after delivery; absence outside that interval is not
claimed. Duplicate consumption, wrong run/asset identity and invalid transfer
must fail closed without playback.

Use finite per-phase and overall deadlines without resetting the overall
budget. On every failure retain partial trace, checkpoint validation outcome,
process identities, frame and position evidence, exit status and cleanup
outcome. Settle only owned processes and children; uncertain settlement is a
failure, not permission to retry or launch another attempt. Publish a result
only after independently checking the recorded identities, one-shot transfer,
fresh B evidence and complete cleanup. Execute only after a separately reviewed
fixture, native binding and platform allocation are available; portable tests
or a healthy replacement do not prove graphics-loss or server recovery.
