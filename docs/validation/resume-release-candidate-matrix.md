# Resume release candidate validation matrix

Source review baseline: `ff831ccc7832d147f77dc4e69006ccda0190fa89`, with the four
test histories merged into this branch. This is a validation plan, not a claim
that an installed artifact, resume, recovery, CI, or release has passed.

## Behavior and evidence boundaries

Ordinary suspend/wake and un-minimize are distinct from GPU reset or renderer
reconstruction. The existing production QML at `src/ui/webview.qml` enables the
web view layer. Commit `fd315620f920582380e808d2805ceefb67ff6d75` describes an
intermittent blank window after un-minimize or suspend, briefly relieved by
cursor motion, and records a historical Arch Linux, Plasma 6, Wayland, AMD
6900XT test. That change remains in source; it does not prove current Windows
or macOS behavior, nor video continuity after a GPU failure.

Production intent state from `ab04afe7` and checkpoint work from `70fee1a6`
records conservative playback state. `PlaybackIntent::automaticResumeEligible`
returns false, and `PlaybackCheckpoint` writes `automatic_resume: false` and
rejects a true value. These are safeguards and state foundations, not enabled
automatic reconstruction. The web renderer fault guard cuts off its channel
after renderer loss (`a8d453f8`, `06992206`); containment is not visible UI or
video restoration. `docs/playback-recovery-acceptance.md` explicitly says the
broader graphics or web-renderer recovery outcome is not delivered. C2a/C2b
and healthy restart work are diagnostic, held, or test-only; a passing fixture
does not establish a production restart policy or recovery from a lost context.

The four merged changes are tests, not production enablement:

| History | Coverage intent | Limit of evidence |
| --- | --- | --- |
| `f86f8882292ff8acb8b4bee5d32b74bd916511e5` | Failed cancellation response after a replaced core in `diagnosticCoreReplacement`. | Fixture observation; no installed playback proof. |
| `d194099a3d987a36e08e4ddd07ac24082187c47f` | `fixtureBridgeControl` uses the graph fixture to exercise grant, load and cancellation. | Diagnostic fixture, not a production recovery path. |
| `e573c7cff9b5711392c9d8bd30e92ffb1142f759` (including parent `20f85c74`) | Cleanup access ownership, containment and invalidation. | Unit-level contract coverage. |
| `b4f5465c6a12e203c0aceeca36251f59336cde55` | `pendingLoadExpires` checks normal graph access is withdrawn before later cleanup polling. | Failure containment assertion, not resume proof. |

## Reproduction record needed before acceptance

The retained historical upstream description above identifies a trigger and
Linux graphics stack, but does not supply an exact media sample, audio and
video tracks/codecs, full machine and display configuration, app artifact
identity, repetition count, captured frame sequence, or current Windows result.
The local issue inventory describes Windows and Apple Silicon black screens as
legacy reports requiring reproduction; it is not a current reproduction.
Capture the affected machine, OS/build, GPU/driver, displays, power state,
window mode, server and app versions, artifact hash, media and track details,
exact suspend/wake or minimize sequence, playback position and pause state,
audio output, and observed UI and video frames. Record the baseline and fixed
artifact separately. Use a predeclared bounded repetition count and retain each
outcome; do not invent a pass count or infer a visible frame from audio alone.

## Validation matrix

| Lane | Required evidence | Current status |
| --- | --- | --- |
| Windows local gates | Build the exact assembled source; run applicable formatting, lint, unit and integration gates and focused tests. Preserve exact commands, identities and failures. | Not run for this assembly. |
| Windows installed artifact | Record package hash/version; install or upgrade using the supported path, exercise ordinary playing and paused suspend/wake and un-minimize on the reproduced setup, verify a visible UI and video frame, advancing position/audio when playing, retained pause/position when paused, and user Stop/new-item precedence. Record bounded repetitions and any regressions. | Missing current reproduction and installed-artifact evidence. |
| Windows reconstruction | Only if separately authorized, use the approved controlled failure/retirement plan and verify same-item video frames, playback continuity, user intent, teardown and all owned processes. Distinguish a truly lost context from a recoverable suspend. | Held; production automatic recovery not enabled. |
| Linux and macOS | Run supported headless build and regression tests on native platforms with exact source/toolchain identities; report skips and platform constraints. GUI suspend/video acceptance needs separate native evidence. | Not run for this assembly. |
| Delivery | Verify exact delivered SHA/ref, required terminal CI jobs and artifact identity; then perform installed acceptance. | Not performed. |

The retained `w1228-proof` case 71 log shows
`healthy_graph_arm.normalPermitBaseline` timed out at 60.02 seconds with no
test output. It does not establish whether the cause was an assertion and UI,
fixture blocking, a clock issue, or a module guard. A future authorized,
hidden, bounded headless diagnostic should capture flushed test-only checkpoints
around fixture setup, grant, permit, load and destruction, use an isolated
owned profile, and preserve the original log. Any process-local Windows error
dialog suppression requires a separate decision; it must not change host-wide
settings. No diagnostic execution is claimed here.
