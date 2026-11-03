# Windows issue triage

Inventory date: 2026-11-03. Baseline: upstream integration commit
`a96ee9dfc2fee7090b644283b2de767b5ee7b9e8`. The baseline includes the Qt
6.9.3 rollback, mpv 20261018 update, ready/locale changes, and volume and
audio fixes. These facts do not establish that any particular report is fixed.

The refreshed upstream listing returned 362 open issues, below the 1,000-item
limit. This inventory covers every open upstream issue returned by that listing
whose title or label meets the rule below, plus Windows environments identified
in issue bodies. It does not treat template examples or Windows mentioned only
as a working comparison as reports of Windows failures.

The title and label inventory covers every open upstream issue returned by a 1,000-item open
issue listing whose title explicitly mentions Windows, Win10 or Win11, or whose
labels include `windows`. It also identifies selected reports with Windows
environments that this mechanical rule misses. An issue being open is evidence
of a report, not evidence that the baseline reproduces it. Version and hardware
details below are those reported upstream; no claim of current reproduction is
made without a separate test. URLs point to the authoritative issue and its
comments. Features, signing and distribution require their own authorization.

## Current-generation reports

| Issue | Report and confidence | Acceptance evidence needed |
| --- | --- | --- |
| [#1264](https://github.com/jellyfin/jellyfin-desktop/issues/1264) | 2.0 startup fails on a D3D feature-level 11_0 GPU; one hardware report, baseline unverified. | Start and render on the reported class of GPU; identify a safe supported fallback or document a proven dependency limitation. |
| [#1248](https://github.com/jellyfin/jellyfin-desktop/issues/1248) | 2.x startup fails on a CPU without AVX2; a legacy build is requested. Hardware and packaging compatibility, not yet reproduced. | Run the actual distributable on a non-AVX2 CPU and verify its full dependency chain; decide compatibility and packaging separately. |
| [#1187](https://github.com/jellyfin/jellyfin-desktop/issues/1187) | 2.0-dev fullscreen transitions and closing after playback malfunction on Windows 11. | Enter and exit fullscreen, stop playback, and close normally, including repeated transitions and multiple monitors. |
| [#1183](https://github.com/jellyfin/jellyfin-desktop/issues/1183) | 2.0-dev playback interferes with Auto HDR and desktop duplication capture. Hardware-dependent; baseline unverified. | Capture during playback and confirm Windows Auto HDR behavior on supported hardware, before and after playback. |
| [#1181](https://github.com/jellyfin/jellyfin-desktop/issues/1181) | 2.0-dev intermittent playback stutter. Baseline and cause unverified. | Play representative SDR and HDR media for a sustained period while measuring frame delivery and audio continuity. |
| [#1174](https://github.com/jellyfin/jellyfin-desktop/issues/1174) | 2.0-era report of severe lag or crash on movement and playback end; configuration recovery was reported. Cause unverified. | Move the window and stop or end playback repeatedly without hangs, crashes, or damaging saved settings. |
| [#1156](https://github.com/jellyfin/jellyfin-desktop/issues/1156) | Master-era NVIDIA fullscreen flicker and ShadowPlay disruption. Driver and hardware dependent. | Exercise playback, fullscreen and window interaction with the reported NVIDIA capture setup; verify capture remains usable. |
| [#1095](https://github.com/jellyfin/jellyfin-desktop/issues/1095) | Windows scrolling and hover responsiveness report; baseline unverified. | Compare scrolling and hover controls with the browser using the same server and content; confirm responsive interaction. |
| [#1079](https://github.com/jellyfin/jellyfin-desktop/issues/1079) | Fullscreen blackouts and HDR state problems on Windows. Hardware-dependent; baseline unverified. | Enter and leave fullscreen and focus a second monitor during HDR playback; verify display state and recovery. |
| [#1062](https://github.com/jellyfin/jellyfin-desktop/issues/1062) | Switching audio tracks stops playback on Windows; reported version is ambiguous. Baseline audio changes are not proof of resolution. | Switch between multiple embedded and transcoded audio tracks during playback; verify correct audio and uninterrupted playback. |

## Legacy reports requiring reproduction

These reports describe earlier Jellyfin Media Player generations or do not
establish a current 2.x reproduction. They remain open and must not be silently
counted as fixed. Reproduce on the baseline and retain the issue-specific setup
before attributing a cause or changing code.

| Issue | Report | Acceptance evidence if reproduced |
| --- | --- | --- |
| [#1146](https://github.com/jellyfin/jellyfin-desktop/issues/1146) | Windows music tracks load slowly or stop. | Play the reported media on Windows and compare browser playback; verify startup and continuity. |
| [#938](https://github.com/jellyfin/jellyfin-desktop/issues/938) | Intermittent crash interacting with UI during playback. | Repeated playback and UI interaction without a crash, with diagnostic evidence. |
| [#837](https://github.com/jellyfin/jellyfin-desktop/issues/837) | Secondary-monitor context menus and restored window bounds fail. | Move between differently positioned displays, use menus, restart, and confirm visible restored geometry. |
| [#762](https://github.com/jellyfin/jellyfin-desktop/issues/762) | Random Windows playback hangs. | Sustained representative playback without hangs, with logs for any recurrence. |
| [#760](https://github.com/jellyfin/jellyfin-desktop/issues/760) | Black screen on Windows and Apple Silicon. | Reproduce on each affected platform; verify video and UI remain visible. |
| [#708](https://github.com/jellyfin/jellyfin-desktop/issues/708) | Windows startup hangs on a white screen. | Restart repeatedly using the affected configuration and verify the UI loads. |
| [#684](https://github.com/jellyfin/jellyfin-desktop/issues/684) | Windows client cannot connect after upgrade. | Connect to the reported server configuration and compare with browser; preserve user configuration. |
| [#675](https://github.com/jellyfin/jellyfin-desktop/issues/675) | Windows 11 fullscreen and Auto HDR behavior. | Exercise fullscreen and display transitions with Auto HDR both enabled and disabled. |
| [#665](https://github.com/jellyfin/jellyfin-desktop/issues/665) | Moving a window outside display bounds freezes the app. | Move partially off-screen and across monitors; confirm interaction and recovery. |
| [#654](https://github.com/jellyfin/jellyfin-desktop/issues/654) | Windows taskbar remains visible in fullscreen. | Enter fullscreen with taskbar visible and verify it is covered; restore normally. |
| [#614](https://github.com/jellyfin/jellyfin-desktop/issues/614) | Media fails after a fresh Windows installation. | Reproduce on a clean supported Windows install and play representative media. |
| [#454](https://github.com/jellyfin/jellyfin-desktop/issues/454) | Cursor disappears in fullscreen. | Move and use the cursor during fullscreen playback and verify expected hiding and restoration. |
| [#366](https://github.com/jellyfin/jellyfin-desktop/issues/366) | Windows user error window. | Reproduce the reported trigger and verify a usable, accurate error and recovery. |
| [#364](https://github.com/jellyfin/jellyfin-desktop/issues/364) | Windows video startup freezes for 30 seconds. | Start the reported media repeatedly and measure startup and UI responsiveness. |
| [#209](https://github.com/jellyfin/jellyfin-desktop/issues/209) | Player icons in a 1.7 prerelease. | Inspect player controls at the reported settings and display scale. |
| [#86](https://github.com/jellyfin/jellyfin-desktop/issues/86) | Always-on-top shrinks the title bar. | Toggle always-on-top and verify window controls and geometry. |
| [#62](https://github.com/jellyfin/jellyfin-desktop/issues/62) | F11 fullscreen and context-menu problems. | Use F11, edit fields and copy/paste with mouse and keyboard; exit fullscreen normally. |
| [#35](https://github.com/jellyfin/jellyfin-desktop/issues/35) | Keyboard shortcuts in 1.2.1 desktop mode; also a question about configuration. | Verify supported shortcuts and documented configuration with keyboard and mouse input. |

## Enhancements and misleading matches

| Issue | Classification |
| --- | --- |
| [#881](https://github.com/jellyfin/jellyfin-desktop/issues/881) | Windows installer signing enhancement; signing credentials and releases are separately controlled. |
| [#214](https://github.com/jellyfin/jellyfin-desktop/issues/214) | Windows ARM64 binaries enhancement; requires dependency and packaging support. |
| [#172](https://github.com/jellyfin/jellyfin-desktop/issues/172) | Microsoft Store distribution enhancement; external publication is separate. |
| [#166](https://github.com/jellyfin/jellyfin-desktop/issues/166) | Windows label, but the original report describes Linux right-click behavior. Cross-platform candidate, not confirmed Windows-specific. |
| [#1221](https://github.com/jellyfin/jellyfin-desktop/issues/1221) | Title mentions Windows as a working comparison; the failure is reported on Linux. Cross-platform candidate, not a Windows failure report. |

## Additional Windows-environment candidates

The title/label rule misses [#1270](https://github.com/jellyfin/jellyfin-desktop/issues/1270)
(2.0 secondary-GPU startup crash), [#1263](https://github.com/jellyfin/jellyfin-desktop/issues/1263)
and [#1065](https://github.com/jellyfin/jellyfin-desktop/issues/1065) (window
geometry), [#1182](https://github.com/jellyfin/jellyfin-desktop/issues/1182)
(playback crash), [#1091](https://github.com/jellyfin/jellyfin-desktop/issues/1091)
(Qt 6 memory growth), and [#1071](https://github.com/jellyfin/jellyfin-desktop/issues/1071)
(artifacts and crash). These require baseline reproduction; #1270 needs the
reported multi-GPU setup. For geometry, resize and reposition, close and reopen
repeatedly, reboot, and test disconnected and rearranged monitors. For #1091,
measure process memory after repeated fullscreen-setting changes and allow for
normal allocation and reclamation before concluding that growth is a leak.

[#1196](https://github.com/jellyfin/jellyfin-desktop/issues/1196) reports that
tone-mapping settings have no apparent effect on Windows 11 with a 2.0-dev build
dated 2026-03-02. It needs a baseline test with controlled HDR media, display,
and configuration; neither the report nor its screenshots establish the cause.
[#1238](https://github.com/jellyfin/jellyfin-desktop/issues/1238) describes
Windows Server library metadata retaining old drive paths after a library move.
Its trigger concerns server indexing and the dashboard; compare with the browser
and server before attributing it to this client.

### Earlier Windows-environment reports

The following body-reported Windows issues are open but are from earlier client
generations or lack an established 2.x reproduction. Issue links identify the
exact media, device, and configuration needed for reproduction. A reported
workaround, upstream discussion, or related fix does not prove baseline behavior.

| Area | Reports and acceptance direction |
| --- | --- |
| Startup and connection | [#1034](https://github.com/jellyfin/jellyfin-desktop/issues/1034) (1.12.0 internationalized server URL after restart), [#1015](https://github.com/jellyfin/jellyfin-desktop/issues/1015) (1.12.0 media DNS failure and misleading error), [#906](https://github.com/jellyfin/jellyfin-desktop/issues/906), [#831](https://github.com/jellyfin/jellyfin-desktop/issues/831), [#828](https://github.com/jellyfin/jellyfin-desktop/issues/828), [#772](https://github.com/jellyfin/jellyfin-desktop/issues/772), [#763](https://github.com/jellyfin/jellyfin-desktop/issues/763), [#724](https://github.com/jellyfin/jellyfin-desktop/issues/724), [#613](https://github.com/jellyfin/jellyfin-desktop/issues/613), [#451](https://github.com/jellyfin/jellyfin-desktop/issues/451), [#306](https://github.com/jellyfin/jellyfin-desktop/issues/306), [#295](https://github.com/jellyfin/jellyfin-desktop/issues/295). Reproduce with the reported server, display and GPU setup; distinguish client errors from server, DNS and configuration failures. |
| Playback and HDR | [#985](https://github.com/jellyfin/jellyfin-desktop/issues/985) and [#953](https://github.com/jellyfin/jellyfin-desktop/issues/953) (1.12.0), [#899](https://github.com/jellyfin/jellyfin-desktop/issues/899), [#847](https://github.com/jellyfin/jellyfin-desktop/issues/847), [#843](https://github.com/jellyfin/jellyfin-desktop/issues/843) (1.11.1), [#792](https://github.com/jellyfin/jellyfin-desktop/issues/792), [#753](https://github.com/jellyfin/jellyfin-desktop/issues/753) (1.11.1 HDR), [#751](https://github.com/jellyfin/jellyfin-desktop/issues/751), [#594](https://github.com/jellyfin/jellyfin-desktop/issues/594), [#593](https://github.com/jellyfin/jellyfin-desktop/issues/593), [#566](https://github.com/jellyfin/jellyfin-desktop/issues/566), [#504](https://github.com/jellyfin/jellyfin-desktop/issues/504), [#437](https://github.com/jellyfin/jellyfin-desktop/issues/437) (1.9.1), [#385](https://github.com/jellyfin/jellyfin-desktop/issues/385), [#338](https://github.com/jellyfin/jellyfin-desktop/issues/338), [#331](https://github.com/jellyfin/jellyfin-desktop/issues/331) (1.7.1), [#140](https://github.com/jellyfin/jellyfin-desktop/issues/140) (1.6.1). Use representative source media and compare browser and native playback; HDR and device-specific cases require the relevant hardware. |
| Audio and devices | [#910](https://github.com/jellyfin/jellyfin-desktop/issues/910) (1.12.0 normalization), [#715](https://github.com/jellyfin/jellyfin-desktop/issues/715) (KVM audio disconnect), [#663](https://github.com/jellyfin/jellyfin-desktop/issues/663), [#495](https://github.com/jellyfin/jellyfin-desktop/issues/495), [#428](https://github.com/jellyfin/jellyfin-desktop/issues/428) (1.9.1 passthrough). Verify continuity, device changes and expected audio level with the specified devices and media; later audio fixes need separate proof against each case. |
| Subtitles | [#902](https://github.com/jellyfin/jellyfin-desktop/issues/902) (supplements #804), [#804](https://github.com/jellyfin/jellyfin-desktop/issues/804), [#679](https://github.com/jellyfin/jellyfin-desktop/issues/679), [#677](https://github.com/jellyfin/jellyfin-desktop/issues/677) (1.10.1), [#643](https://github.com/jellyfin/jellyfin-desktop/issues/643), [#568](https://github.com/jellyfin/jellyfin-desktop/issues/568), [#398](https://github.com/jellyfin/jellyfin-desktop/issues/398) (1.9.0), [#251](https://github.com/jellyfin/jellyfin-desktop/issues/251) and [#187](https://github.com/jellyfin/jellyfin-desktop/issues/187) (1.7 prereleases), [#249](https://github.com/jellyfin/jellyfin-desktop/issues/249). Reproduce selection, formatting, external tracks and edits with the reported files; separate client rendering from server delivery. |
| UI and library | [#1069](https://github.com/jellyfin/jellyfin-desktop/issues/1069) (1.12.0 middle-click scrolling), [#845](https://github.com/jellyfin/jellyfin-desktop/issues/845) (1.11.1/1.12.0 ebooks), [#794](https://github.com/jellyfin/jellyfin-desktop/issues/794), [#791](https://github.com/jellyfin/jellyfin-desktop/issues/791) (1.11.1 drag-and-drop), [#718](https://github.com/jellyfin/jellyfin-desktop/issues/718) (1.11.0 refresh), [#704](https://github.com/jellyfin/jellyfin-desktop/issues/704), [#645](https://github.com/jellyfin/jellyfin-desktop/issues/645) (1.10.0), [#619](https://github.com/jellyfin/jellyfin-desktop/issues/619), [#520](https://github.com/jellyfin/jellyfin-desktop/issues/520) (multi-monitor fullscreen), [#404](https://github.com/jellyfin/jellyfin-desktop/issues/404), [#225](https://github.com/jellyfin/jellyfin-desktop/issues/225). Compare browser behavior where appropriate and test the specified interaction, screen layout or library state. |

[#283](https://github.com/jellyfin/jellyfin-desktop/issues/283) requests gamepad
track selection and is an enhancement, not a confirmed defect. Comments on
some older reports suggest server behavior, custom CSS, or configuration as
possible causes; those suggestions require verification. The inventory does
not establish that any open issue is fixed.

## Work and proof boundaries

Geometry/fullscreen, nonmutating fallback loading, and logging deadlock fixes are
queued for investigation; their presence in this plan is not proof of a fix or
of causality for any issue. Validate the resulting integrated build against
focused reproducible cases and relevant regression gates. Record exact build
identity, operating system, display and GPU configuration, media and server
versions, and outcome. Report untested hardware, external dependency limits,
and remaining open issues explicitly. No upstream issue, release, signing, or
distribution state is changed by this document.
