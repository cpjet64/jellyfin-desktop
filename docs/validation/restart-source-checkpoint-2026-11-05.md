# Restart source checkpoint — 2026-11-05

This is a source and headless-test checkpoint, not a demonstration of playback
recovery. The implementation reviewed is
[`7972f30f1851263f150b1b1d6a950041d79fc360`](https://gitlab.curtpme.com/cpjet64/jellyfin-desktop/-/commit/7972f30f1851263f150b1b1d6a950041d79fc360).
The requested outcome remains a visible interface and picture with the same
playback continuing after a recoverable graphics failure, while preserving
current user intent and Linux/macOS regression coverage.

## Implemented boundaries

- Private Player observation and restoration code correlates ownership and
  cleanup, seek results, playback restart and a position-qualified paused
  snapshot. Headless tests use real `PlayerComponent` boundaries with injected
  events and dispatch; they do not establish actual mpv playback. Observation
  and restoration are unavailable in the external-events-OFF configuration.
- Fixture frame acceptance binds copied render records to process, core,
  context, entry, phase and seek, retains a baseline near frame zero and a
  bounded target after the armed serial, and correlates seek, restart and
  snapshot evidence. It checks the generated counter and moving square, image
  geometry, orientation, grayscale and opacity. Synthetic images exercise this
  logic; actual FBO alpha and native readback remain unproved. Render flags and
  a successful render alone do not qualify an image. The key expresses expected
  fixture ownership, not an image identity returned by mpv; mapping an actual
  B capture to that ownership remains unproved.
- Fixture-owned process and channel primitives, framed evidence and child
  control, cancellation, cleanup and broker state are implemented with
  injected operating-system tests. The fixture adapter connects the protocol
  to Player boundaries. These tests do not establish actual inherited channel
  identity or a functioning parent and two child processes.
- Public automatic resume remains disabled. The existing copied FixtureCore
  uses external events OFF and cannot use the ON-only restoration path. A new
  healthy fixture core design has not been approved or implemented.

## Local evidence at the earlier source

At [`a10246ee970682c7d3fc4e943ba202a1f7a99e21`](https://gitlab.curtpme.com/cpjet64/jellyfin-desktop/-/commit/a10246ee970682c7d3fc4e943ba202a1f7a99e21),
the retained W831 results record successful Windows static ON/ON and system
OFF/OFF builds and 48/48 and 45/45 headless CTest cases respectively. The
fixture copy suite passed 9 tests, and its dependency audit reported 46 copied
FixtureCore dependency records with no original player source or header. Its
fixture build completed; it was not run. The retained fixture executable was
independently hashed as
`20802b335d4ce6edd623b63eb344331f0f2c274201a60a0334ceb76b8a66e702`.
These results apply to that source, not automatically to the later checkpoint.

The earlier W825 system build failed compiling the observation test because
`PlayerComponent::m_mpvGeneration` was unavailable in that configuration.
The later W831 OFF/OFF build and tests passed after the guarded setup change;
this does not show the OFF restoration capability exists.

At `7972f30f1851263f150b1b1d6a950041d79fc360`, W834 records successful full
Windows static ON/ON and system OFF/OFF builds, followed by 52/52 and 49/49
headless CTest cases respectively, with no skipped or not-run cases. The newly
registered driver tests and changed channel tests exercise injected behavior;
they do not prove real pipes or native process execution. The independently
checked SHA-256 identities are:

| Evidence | SHA-256 |
| --- | --- |
| Static application | `9e9e62e76c4b5927ee76f02add633d5765dae3bcb5a53714819b26b4b3e8e4d4` |
| System application | `f8c28ca7aa98dd3fa921712a442f943e690640cb8f9d7c2611fbe4829921566e` |
| Static CTest log | `29a8d6439b31c7f356b21888215406a28875ad4437a392adf8ba30278b039dbc` |
| System CTest log | `db878f30041a67d6014f067592037275ce19275e98dcc8aea4f72fd69df221e3` |

## Cumulative parent and source-player checkpoint

At `e440802dbc2ab36e7981edcd899fb1e5ac9207aa`, W843 completed full Windows
static ON/ON and system OFF/OFF builds with `cmake --build <tree> --parallel 2`,
then headless `ctest --test-dir <tree> -LE gui --no-tests=error
--output-on-failure --timeout 60`. Static passed 54/54 and system passed 51/51,
with no skipped or not-run cases. Source remained unchanged throughout;
evidence is retained under `deps/w843-proof/` in the integration worktree.

The actual Windows broker operations and parent composition and the shared
source-player adapter are registered and passed their headless tests in both
local configurations. Their operating-system interfaces are injected: this is
not evidence of real pipes, child processes or playback. OFF remains an
unavailable-behavior check, not positive restoration proof. The shared
source-player test is required by Linux/macOS gates, whose execution for this
checkpoint remains pending.

| Evidence | SHA-256 |
| --- | --- |
| Static application | `e33af3c462379416e3c8b403044b07ada4ec12778bb2d3ec90d7b1f86f74d469` |
| System application | `f8c28ca7aa98dd3fa921712a442f943e690640cb8f9d7c2611fbe4829921566e` |
| Static CTest log | `bab01749391e997ee96a5ff61b96e3f2bab854b2eb8809689c5481dbe1d0a72b` |
| System CTest log | `d0a8e9d1649da5f47795a18dc81bb29c89a8fb4982f9e98045b1289958837367` |

The latest separately proposed healthy-core design question returned a system
error without a decision; the new core and native attempts remain held.

## Remaining proof

No operating parent/A/B executable, actual channel inheritance, native fixture
frame, GUI, WebEngine, server playback, graphics crash, recovery, or qualified
unpause is established by this checkpoint. Native Windows, Linux and macOS
platform obligations, including actual rendering and desktop controls, remain
separate from portable or Windows-hosted headless checks. Actual driver-reset
and permanently lost-context behavior, production policy, and hands-on
acceptance remain open.

[Pipeline 32699](pipeline-32699.md) is the last recorded allocated pipeline:
all seven jobs reached terminal failure at source
`52391b95f59bc8820845f1cc7509d80f6c132b88`. Its Windows input mismatch and
Linux validator predicate remain unidentified; no newer CI result is claimed.
