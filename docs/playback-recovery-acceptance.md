# Playback recovery: behavior and acceptance

## Requested outcome

After a recoverable graphics or web-renderer failure, Jellyfin Desktop should
restore a visible interface and video picture and continue the same playback.
Playing media should advance; paused media should retain its position and pause
state. Stop, closing playback, a new item, and changed track selections must not
be undone by delayed recovery work. Windows changes must retain native Linux
and macOS regression coverage.

This outcome is not yet delivered. Passing web-renderer containment tests does
not establish recovery from loss of the native OpenGL context or a GPU reset.

## Current boundaries

- C1 establishes one event consumer and safe controller binding. Its approved
  fresh-controller AC3 correction preserves the old core and configures the new
  core exactly once. Renderer retirement must preserve the existing binding,
  event owner, hook continuations, and AC3 configuration.
- C2a retains the accepted controlled-retirement design, not automatic recovery.
  Its diagnostic build option defaults to OFF and requires bundled MpvQt with
  external event ownership. System MpvQt builds remain outside this capability.
- C2a reports typed create/render failures and renderer generations. Its
  controlled retirement acknowledges only an actual, safe free. Earlier
  single-thread C2a and offscreen native passes are historical observations;
  they do not satisfy the corrected thread contract and must be rerun after the
  Qt-only thread lifecycle is reviewed. They do not demonstrate recovery from
  an actually lost context.
- C2a runs only in a diagnostic build without application or package targets.
  The fixture retains the whole item, window, core, original GL context, and
  surface until retirement completes. It does not establish a production
  shutdown policy or permit arbitrary QML/window destruction during retirement.

## Controlled-retirement obligations

1. Free each render context once on its owning thread with its original OpenGL
   context current. Report an unavailable original context as blocked; never
   acknowledge successful retirement in that state.
2. Close callback admission, unregister the callback, account for callbacks in
   flight, and reject stale queued work. Registration may invoke the callback
   immediately. No callback may call an unsafe mpv API or wait for the GUI.
3. Keep the same core, controller, event-consumer identity, and binding
   generation during retirement. Do not reinitialize the Player.
4. Exercise the Player's delayed hook continuation through a test-only delay
   seam. A hook owned by a secondary test client is separate evidence.
5. Exercise negative create/render results, one failure per generation, Stop
   during retirement, Stop during a pending load hook, acknowledgement ordering,
   callback races, and the actual events and state changes caused by retirement.
6. Close the secondary observer client before core destruction, and keep core
   destruction behind safe render-resource retirement. Do not suppress real mpv
   events to make playback appear uninterrupted.
7. A missing original context means `BlockedNoContext`, zero frees/core
   terminations, and no acknowledgement. The fixture may restore that same
   context and explicitly retry the same generation once. A scheduled job that
   never executes also has no acknowledgement; report bounded missing progress.
8. Test permanent context unavailability with fake resources only. Every real
   native pass must legally retire, tear down the item, terminate the core, and
   exit naturally with zero owned processes. Irreversible context loss is a
   failure, not demonstrated recovery.

The pinned mpv implementation disables active video when its render context is
freed. A surviving handle or successful free/create pair is therefore not proof
of restored picture or continuity. Its callback setter invokes callbacks while
holding the same lock used for callback replacement; application-queued work
still needs its own lifetime and generation protection.

## C2b diagnostic reconstruction obligations

C2b is approved only as a diagnostic experiment: one explicit replacement of
retired generation G1 with G2 and restoration of a locally generated clip. It
does not authorize production automatic recovery or imply that a lost GL
context can be recovered. Keep the existing core, C1 binding, event owner,
and original GL context; G1 must be terminal and its shared owner retained
until outstanding work is safe. Create G2 on the actual owning render thread.

Before retirement, capture the local playback recipe, position, pause state,
and latest user intent. Restore a paused clip first and verify its visible
image and position before any requested resume. Permit one owned replay only
when retirement actually made the original playback terminal; do not duplicate
a still-active playback. Reject stale callbacks and events from G1, and honor
Stop during retirement or restoration without restarting media. Verify the
actual Player hook continuation returns; a secondary-client hook is not a
substitute. Report failures once per generation and perform legal cleanup.

Three static diagnostic jobs are forthcoming. They are distinct from the six
required native platform/configuration obligations below, and neither set
alone establishes the requested product behavior.

The corrected native proof proceeds in order:

1. Review and pass the Qt-only thread-lifecycle proof.
2. With the GUI thread held, prove render-thread independence using actual mpv
   render entry and return and fresh images.
3. Rerun all four C2a scenarios under that corrected lifecycle.
4. Run one playing C2b scenario, then the remaining seven C2b scenarios.

Do not treat a later step as passed on the strength of the earlier single-thread
observations. Permanent context loss remains blocked, and production automatic
recovery remains on hold.

Pinned source: [mpv render contract](https://github.com/mpv-player/mpv/blob/01e2533f3143b8940857ca419b0b367f583e05ca/include/mpv/render.h)
and [render-context implementation](https://github.com/mpv-player/mpv/blob/01e2533f3143b8940857ca419b0b367f583e05ca/video/out/vo_libmpv.c).

## Rendering and platform evidence

Playing tests must contain recognizable generated-fixture content in two
post-fault frames, show different images, and show advancing playback position.
Paused tests retain the picture and position. All scenarios retain native Stop
assertions and bounded, owned-process cleanup. Test media is generated locally;
these tests do not require a production server, account, or watch history.

All runners are headless. A valid GL context does not prove window exposure, and
absence of a monitor does not establish whether a GUI session exists. Keep these
obligations separate: native build/core, actual offscreen rendering, and desktop
or platform controls. Record passed, failed, blocked, and not-run distinctly.

The six native platform/configuration jobs remain required. A separate required
product-acceptance check must reject missing evidence, failed cases, unresolved
required capabilities, and mismatched source/configuration provenance. An
unverified desktop receipt must not grant product acceptance.

Linux rendering uses an owned display and private D-Bus session. macOS mpv/runtime
and Cocoa checks retain the verified existing `runner` session requirement.
Neither host installation nor session creation is implied by test adaptation.
Windows local desktop evidence and service-runner offscreen evidence prove
different paths; neither silently substitutes for the other.

## Open assumptions and acceptance limits

- C2a begins with a controllable valid GL context. Actual driver-reset recovery,
  permanently lost contexts, and a production restart policy remain unproved.
- Restoration must later preserve playback identity and current user intent,
  including paused playback, Stop, and changes made while recovery is pending.
- Adoption of an existing native session by a replacement web document remains
  unresolved. So does the race between healthy web `removeMediaDialog` and a
  native Stop; neither is established by the diagnostic experiment.
- Shared Windows MpvQt imports libmpv eagerly. Its package does not inherit the
  bundled static build's non-AVX2 fallback claim.
- Native Linux/macOS execution, desktop-control receipts, exact final CI proof,
  and hands-on user acceptance remain separate from local Windows test results.
