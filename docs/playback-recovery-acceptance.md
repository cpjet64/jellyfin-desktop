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
- C2a is an approved, controlled retirement experiment, not automatic recovery.
  Its proposed build option must default to OFF and require bundled MpvQt with
  external event ownership. System MpvQt builds remain outside this capability.
- C2a reports typed create/render failures and renderer generations. It accepts
  an explicit retirement request and acknowledges only an actual, safe free.
  Reconstructing a renderer, reloading media, and automatic retries need a later
  reviewed implementation and acceptance evidence.
- The core/render-resource lifetime owner for blocked retirement is still under
  design review. Neither returning from an item destructor with dangling state,
  freeing under a replacement GL context, nor destroying the core first is a
  valid implementation of blocked retirement.

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
6. Keep core destruction behind safe render-resource retirement. Do not suppress
   real mpv events to make playback appear uninterrupted.

The pinned mpv implementation disables active video when its render context is
freed. A surviving handle or successful free/create pair is therefore not proof
of restored picture or continuity. Its callback setter invokes callbacks while
holding the same lock used for callback replacement; application-queued work
still needs its own lifetime and generation protection.

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
- Shared Windows MpvQt imports libmpv eagerly. Its package does not inherit the
  bundled static build's non-AVX2 fallback claim.
- Native Linux/macOS execution, desktop-control receipts, exact final CI proof,
  and hands-on user acceptance remain separate from local Windows test results.
