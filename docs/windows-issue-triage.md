# Windows issue triage

Inventory date: 2026-11-03. Baseline: upstream integration commit
`a96ee9dfc2fee7090b644283b2de767b5ee7b9e8`. The baseline includes the Qt
6.9.3 rollback, mpv 20261018 update, ready/locale changes, and volume and
audio fixes. These facts do not establish that any particular report is fixed.

This inventory covers every open upstream issue returned by a 1,000-item open
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

Other open reports can describe Windows in their body, compare platforms, or use
an issue template mentioning Windows. They are not automatically Windows-owned.
Investigate cross-platform reports when their observed behavior overlaps a
reproduced Windows defect; do not infer that this finite title/label inventory
proves all such reports have been resolved.

## Work and proof boundaries

Geometry/fullscreen, nonmutating fallback loading, and logging deadlock fixes are
queued for investigation; their presence in this plan is not proof of a fix or
of causality for any issue. Validate the resulting integrated build against
focused reproducible cases and relevant regression gates. Record exact build
identity, operating system, display and GPU configuration, media and server
versions, and outcome. Report untested hardware, external dependency limits,
and remaining open issues explicitly. No upstream issue, release, signing, or
distribution state is changed by this document.
