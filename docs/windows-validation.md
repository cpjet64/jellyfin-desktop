# Windows repair validation — 2026-11-03

Baseline: upstream `a96ee9dfc2fee7090b644283b2de767b5ee7b9e8`.
The unchanged baseline also built successfully and passed its four CTest suites.
Its executable SHA-256 was
`4760c660ea8eadc61f12de23e6291ac8df79bce015b1143e5d13b07320dd9d05`.
The original checkout and its branch were preserved. This is a candidate batch,
not a claim that all open Windows reports are resolved or ready for user acceptance.

## Build environment

- Windows 11, x64; MSVC 19.44.35229 / Visual Studio 2022 Build Tools.
- Qt 6.9.3, including WebEngine, WebChannel, and Positioning.
- Primary and fallback mpv: `20261018-git-01e2533f31`, from the existing
  Windows setup script's SourceForge URLs.
- mpvqt submodule: `2ebbd38bf65d01be9c55d673ecd43c8bb9a6fc8e`, built statically.
- One project-local dependency installation; no global tool installation,
  persistent PATH changes, WSL, or Docker.

## Native checks

From the integration checkout, with the existing VS 2022 x64 developer environment:

```powershell
$deps = 'E:/CursorAI/JellyfinDesktop/dev/windows/deps'
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_INSTALL_PREFIX=build/output `
  "-DQTROOT=$deps/qt/6.9.3/msvc2022_64" `
  "-DMPV_INCLUDE_DIR=$deps/mpv/include" `
  "-DMPV_LIBRARY=$deps/mpv/libmpv-2.dll.lib" `
  -DCHECK_FOR_UPDATES=ON -DUSE_STATIC_MPVQT=ON
cmake --build build --parallel 4
$env:PATH = "$deps/qt/6.9.3/msvc2022_64/bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$deps/qt/6.9.3/msvc2022_64/plugins"
ctest --test-dir build --output-on-failure --timeout 60
python tests/windows_mpv_loader_integration.py
dumpbin /dependents 'build/src/Jellyfin Desktop.exe'
cmake --install build
```

The application build and seven CTest suites passed (zero skipped suites).
They cover existing system/log/settings/display behavior, saved geometry and
fullscreen transitions, logging failure paths/concurrency, and flag composition.
The window tests use Qt's offscreen platform; they do not establish real desktop,
reboot, multi-monitor, playback-end, or queued-episode behavior.

The native loader harness passed real delay-import dispatch to distinct fixture
DLLs, repeated loads, unchanged hashes, missing/corrupt DLLs, missing dependency,
missing export, and rejection of a primary DLL found only on PATH. Read-only file
attributes were initially tested. The subsequent ACL fixture denies actual file
creation, replacement and deletion to the executing Windows token while both
loader variants execute twice with unchanged DLL hashes. Original inherited ACLs
are saved and compared after restoration before cleanup. The integrated fixture
passed locally; an affected non-AVX2 CPU remains untested.
The final application lists mpv only as a delay-load dependency and no shared
mpvqt dependency. Inspection of the provisioned 138 Qt DLLs found no mpv imports;
this is not a complete recursive system/plugin dependency audit.

Initial installation staging warned that the legacy source-relative VC runtime
location was absent. The follow-up packaging correction and actual package proof
below supersede that incomplete staging result. No global Inno installation or
clean-machine installer execution was performed.
The Windows workflow now runs CTest and the loader harness before packaging,
and propagates failures instead of continuing to a later successful command.

An initial restricted-context compiler check failed with C1902; the same native
configuration succeeded outside that context. Two real logging regressions were
then found and corrected: retained QTemporaryFile handles prevented Windows
promotion, and directory obstructions could be renamed as though they were logs.
The passing suites include those cases and a Windows locked-archive test.

## Actual application diagnostic attempt

`tests/windows_desktop_smoke.mjs` accepts explicit absolute build and Qt paths.
It uses task-local portable data and a loopback debugging endpoint. It is a
diagnostic tool, always returns a non-success status, and cannot certify acceptance.

Four attempted offscreen application launches (normal, CLI GPU disable,
environment GPU disable plus a setting-generated flag, and quoted environment
flags) exited with `0x80000003` before the debugging endpoint became available.
The application reported that the offscreen plugin cannot create an OpenGL
context. No Chromium backend, initial web UI, or clean UI exit was verified.
The temporary portable marker was removed; diagnostic files were retained locally.

## Native Windows desktop proof

The follow-up `windows_hidden_desktop.cpp` helper creates a temporary non-input
Windows desktop without switching the user's desktop. It assigns the application
to a kill-on-close job before resuming it, and fails unless the application exits
normally with code zero and its descendants drain naturally. The Node harness
stores full browser observations and console diagnostics in unique local run
directories. It requires the actual server-selection page and native exit method.

Compile the helper with the existing VS 2022 x64 environment:

```powershell
cl /nologo /std:c++14 /W4 /WX /EHsc tests/windows_hidden_desktop.cpp `
  /Fe:build/hidden-desktop.exe /Fo:build/hidden-desktop.obj user32.lib
node tests/windows_hidden_desktop_smoke.mjs `
  "$PWD/build/hidden-desktop.exe" "$PWD/build" "$deps/qt/6.9.3/msvc2022_64"
node tests/windows_hidden_desktop_smoke.mjs `
  "$PWD/build/hidden-desktop.exe" "$PWD/build" "$deps/qt/6.9.3/msvc2022_64" --log-failure
```

On the tested RTX 3080 Ti host, all four candidate cases and the log-failure case
passed the page, native-exit, zero-process and no-forced-cleanup assertions.
Normal and quoted-argument cases used ANGLE D3D11 with GPU compositing enabled.
CLI and environment GPU-disable cases reported software compositing; the latter
also retained the setting-generated `--disable-pinch`. The quoted user-agent
remained one quoted argument in the actual browser command line.

The unchanged baseline's normal and CLI-GPU-disable cases both rendered the same
page and exited normally. Both still used GPU compositing, and the baseline CLI
case lacked `--disable-gpu` in the browser command line. This supplies a native
before/after observation for the flag repair.

With a regular file obstructing the candidate profile's log directory, stderr
reported the logging failure, the server-selection page remained usable, and
native exit completed normally. No server login or playback was performed.
Software-compositing cases still reported GLES/shared-context diagnostics:
initial UI success does not establish mpv rendering or playback compatibility.
These tests belong on the local desktop workstation, not headless CI runners.

## Outstanding acceptance

- Server connection, representative playback, stop, and playback-end restoration
  need an authorized test account and media. No before/after
  real-playback claim is made.
- Production watch-history changes were not assumed authorized.
- Reboot persistence, display rearrangement, HDR/capture and mixed-GPU cases,
  sustained memory growth, and affected non-AVX2 CPU compatibility remain open.
- No upstream issue was closed, and no release, signing, or deployment was performed.
- At the user's request, GitLab is now primary and the public GitHub fork is a
  downstream review mirror. The exact GitLab pipeline and artifacts
  must be verified separately; local success is not remote CI proof.

See [the issue inventory](windows-issue-triage.md) for report-specific acceptance.

## Follow-up review corrections

QML fullscreen wrappers now call the native window manager, so keyboard and DOM
requests use its restore behavior. After correcting readiness and modifier-key
delivery in the hidden-desktop diagnostic, posted Windows F11 and Alt+Return
messages each entered fullscreen and restored the maximized window. The actual
application then exited through its native bridge with code zero, no forced
cleanup and no remaining job processes. A further three-launch run passed both
shortcuts in normal state and restored the same outer bounds (120,120,1080,800)
on the next two launches without resizing. Per-launch serialized settings were
preserved alongside observed bounds. Physical keyboard input, reboot and display
changes remain separate checks.

The seven native suites passed again after the QML change and geometry-helper
extraction. The logging suite now forces a same-thread Qt diagnostic during a
production rotation failure in a bounded subprocess, checks token censoring in
the fallback output, and verifies subsequent file logging.

Windows packaging now accepts explicit runtime DLL and redistributable paths;
missing required inputs fail at packaging time without blocking ordinary builds.
Two native CMake staging fixture tests passed, including paths with spaces and
missing-input failure before staging.

## Cumulative local package proof

`cmake --build build --target windows_all --parallel 4` completed successfully
using project-local Inno Setup 6.7.3 and Microsoft's signed VC redistributable.
Explicit runtime inputs pointed to the shared project-local DLLs and installer.
The source was `04a2c63228ae681a5f9c9dcb27471fa93d9708bf`; changes through
`1e029623` only added diagnostic tooling and evidence. The seven native suites passed again.

| Artifact | SHA-256 |
| --- | --- |
| JellyfinDesktop-2.0.0-dev-x64.exe | `d1507522031247bd800b663164f4453563e4294551958f4b748095e734b14fcf` |
| JellyfinDesktop-2.0.0-dev-x64.zip | `d2b171bb4a97aa0b984a3e4b6e079bd7bf89f362c57c2c2de20d9a8657451c9f` |
| Extracted Jellyfin Desktop.exe | `8ba68d256389b2209d7384d95b31e6e5963e973ac8cab29b5fdefc2bcc03c42d` |

The actual ZIP was extracted into a fresh task-owned fixture. The packaged-mode
diagnostic launched it with PATH restricted to Windows system directories and
development Qt/QML environment overrides removed. Its initial server-selection
page loaded using bundled QtWebEngineProcess and ANGLE D3D11; native exit was
zero, with no forced cleanup or remaining job processes. Full DLL hashes and
observations were preserved locally. The shipped empty portable marker was
temporarily removed and recreated by the harness; it only selects portable mode
and does not encode a cache path. Shipped executables/DLLs were
not changed. No installer execution, clean-machine proof, signing, release,
server connection, or playback is implied by this local portable launch.

## Second source review and corrected diagnostics

The input plugin now retains and removes its own event callbacks and native
signal connections, clears its timers and handles destruction before API setup.
Two Node regressions passed using the actual dispatcher semantics: callbackless
`Events.off` does not remove ordinary callbacks. Actual connected-web playback
and QWebChannel lifecycle behavior remain acceptance gaps.

The installer checks registered x64 runtime state and core DLL versions against
the bundled versions, handles existing app-local runtime files and checks the
result of an opted-in central install. Its two compilation fixtures passed with
real versioned inputs and unversioned-input rejection. These tests do not execute
installer decisions; incomplete/older/newer runtime and upgrade scenarios still
need an isolated Windows environment, without altering this workstation's runtime.

The corrected native harness gives independent cases fresh profile/cache IDs,
shares one new ID only within each three-launch geometry sequence, handles helper
errors and completion with bounded waits, and preserves owned-marker boundaries.
Missing-helper and early-exit fixtures passed. Four GPU/quoted-argument cases,
three geometry launches and obstructed logging passed again with clean native
exit and zero job processes. It now captures actual page screenshots; inspected
normal and CLI-GPU-disable captures show the expected initial selection form.
The package mode sets its working directory to the extracted app directory and
reuses the shipped empty marker without claiming that marker isolates caches.

Initial GitLab pipeline `32054` at `1e029623` failed because the runner lacked
7-Zip. Verified job-local tool fallbacks and regression commands were prepared
and exercised locally; the correction needs a new exact-SHA pipeline result.

The second cumulative package build also passed (`windows_all`, source
`cf1c3e4d1dd8886248f957df6f607335d27e0985`). Its portable package passed the
corrected readiness and screenshot check with the shipped marker preserved,
application-directory working directory and system-only PATH. The screenshot
was inspected, and native exit drained all job processes without forced cleanup.
This supersedes the earlier package startup observation, while installer decision
execution and connected playback remain untested.

| Second-batch artifact | SHA-256 |
| --- | --- |
| Installer | `f1ce89f3dc06ce07725a41e8e75319adb0430032083c3b71cfaa59c50a0af6bd` |
| Portable ZIP | `41dd2b22ec301541978b918bd48486fe0c14fed85c3889f60b2d0a97a34a8b65` |
| Packaged executable | `df194f34afdde872eee7ee3db7b7410a88864178a05699005f6e4df1a77c92b2` |

## Third review follow-up

Source `217dafdfed722aab5a77a22836b009d23d506644` clears timers on a matching
manager-only player stop and preserves the redistributable's 3010 restart request
through Inno's `NeedRestart` event. Four input-plugin tests and eighteen release
mirror tests passed. A fresh native build, all seven CTest suites, two packaging
fixtures, two installer compilation fixtures and the hidden-desktop lifecycle
fixture passed. `windows_all` produced the installer and portable ZIP below.

The extracted ZIP rendered the server-selection form with system-only PATH,
then exited with code zero and no remaining job processes. Its screenshot was
visually inspected. Logs, packages and hashes remain in the local
`deps/windows-validation-proof/candidate-217dafd` evidence directory. Generated
build output was cleaned after preservation. Installer execution, reboot handling
in a clean Windows environment and connected playback remain unverified.

| Third-batch artifact | SHA-256 |
| --- | --- |
| Installer | `1b3bcffe48357ad7c9983de9d0affbebd5d67534420b448c6b6b5c53b7bbd100` |
| Portable ZIP | `a439a68458f3f211db8a04d8e5e89edb0f9abdfc906775c95de747b98569a60a` |
| Packaged executable | `3edea68f87466013ad7a6a785d7d93986f8c8830d8109c512dfcc5bf293e9fe1` |

Pipeline 32058 (`6e52aeb`) failed when SourceForge returned HTML instead of the
mpv archive; 7-Zip rejected it with exit code 2. Commit `138d7b3` validates direct
downloads and their pinned hashes before Qt setup. Both real archives downloaded
and extracted locally. Subsequent exact pipeline status was unavailable during
repeated GitLab HTTPS handshake timeouts; SSH pushes and downstream mirroring
continued to work. Local success does not establish remote CI success.
