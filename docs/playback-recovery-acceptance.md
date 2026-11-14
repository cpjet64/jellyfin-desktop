# Playback recovery: behavior and acceptance

## Requested outcome

After a recoverable graphics or web-renderer failure, Jellyfin Desktop should
restore a visible interface and video picture and continue the same playback.
Playing media should advance; paused media should retain its position and pause
state. Stop, closing playback, a new item, and changed track selections must not
be undone by delayed recovery work. Windows changes must retain native Linux
and macOS regression coverage.

The production automatic path is under development and is not accepted until
the installed-artifact evidence below passes. Passing web-renderer containment
tests does not establish recovery from loss of the native OpenGL context or a
GPU reset. Preserve the web-renderer guard and report unsupported renderer
capabilities as blocked; never infer a safe retirement from a missing context.

## Automatic recovery acceptance plan

Evaluate the current user session every 500 ms. A qualified moving-video stall
requires five seconds without source-frame progress while the playback clock
advances. Pause, loading, seeking, buffering, an unexposed or hidden window,
audio-only media, EOF, reset, and sparse or still images suppress this stall
timer and reset its baseline. An attributable operation timeout or qualified
stall selects one owned reload only if the original render thread and context
produce a bounded usability acknowledgement. Lost context or failure to obtain
that acknowledgement within the existing 15-second attempt deadline is
blocked graphics recovery. One attempt is allowed per user-created session;
internal pause and load commands do not change intent revision or rearm it.

Preserve desired user pause separately from temporary native recovery pause.
Stop, exit, new item, newer seek or track selection, and stale replies cannot
revive old intent. Load paused at the captured target with precise seeking,
require the matching accepted native entry, its scoped file-loaded sequence,
fresh seekability, pause and position observations, then an attributed newer
native source frame rendered and associated with its Qt submission. Resume
only if the latest user intent still requests playing. A usable renderer may
retain its generation across reload; native load/entry, attempt and frame
identities must still be new and attributable. Do not invent an explicit seek
reply. A failed or unrun retirement cannot be acknowledged as successful.
Preserve authenticated loads and tracks without placing credentials or
authenticated URLs in logs or persistent evidence.

If the original context is unavailable, retain one process-lifetime
quarantined core and renderer generation, revoke its authority, request an
owned stop where possible, and refuse competing playback or further replacement.
Quarantine is containment, not restored playback or a fix for full context loss.

An established accepted item may also qualify for one reopen after 30 seconds
of attributed buffering or media-read stall while the user requests playing.
Only fresh native observations for the current entry and event epoch establish
the interval; missing, stale, or repeated old observations do not prove no
progress. Reset on meaningful attributed position or source-frame progress.
User pause, EOF, deliberate stop, known permanent authentication or media
errors, and a changed user intent do not qualify. Preserve native errors as
observed; do not rename a generic failure as a network timeout. This reopen
uses the same paused-first restoration, single 15-second recovery budget and
one attempt per user-created session as the other supported classes. Thirty
seconds is an initial threshold requiring runtime validation.

The first operation-timeout class is an owned absolute exact seek on an already
accepted eligible item. Bind its full-width request and reply IDs to the user
session, intent revision, native entry, renderer and native seek discontinuity.
The correlated command reply has five seconds and the complete postcondition
has fifteen seconds, both measured from the same admitted dispatch. A reply
alone does not prove position or frame submission. Require fresh qualified
native position within the predeclared tolerance of the requested target and
an operation-attributed submitted frame. On a recoverable timeout, the one
paused-first reopen targets the requested seek position; retain the earlier
checkpoint as evidence rather than substituting it. A newer seek, Stop, new
item or destruction revokes old authority; a paused seek remains paused.

Before hands-on acceptance, record the exact package version, source identity,
artifact hash, machine, GPU, driver, display and server configuration. Generate
a local clip with moving recognizable content and audio; retain its generation
recipe and hash. Inject each fault through the actual adapter and renderer path,
not a direct recovery call or synthetic frame. Require five consecutive
successful recoveries for each selected deterministic timeout and moving-video
stall fault class, and the qualified buffering/read-stall class once included,
plus paused, Stop, new-item, cancellation, and failed or
repeated-fault cases. Use the normally enabled controller; no manual minimize,
wake, or restore action may cause recovery. Record each trigger, observed sequence, timing,
outcome and teardown. Playing acceptance requires recognizably different
post-fault frames, advancing position, and expected audio. Paused acceptance
requires the correct picture, position and paused state. Predeclare a maximum
absolute restored-position error of two seconds for this clip and do not widen
it after execution. Record audio ownership and verify no competing playback.
Screenshots alone,
blackness, audio alone, a configured video output, and fake frames do not prove
visible recovery. Record native Linux and macOS headless regressions separately;
they do not substitute for installed visible-video acceptance. The legacy
JMP 1.12 / Qt 5 report is historical baseline context; an unpredictable field
trigger is not a reason to skip the controlled production-path fault tests.

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
