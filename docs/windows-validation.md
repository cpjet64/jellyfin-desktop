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

Native installation staging through `cmake --install build` succeeded. Inno Setup
is absent locally, so a local installer build and clean-machine installation remain
unperformed. The staging run warned that the legacy source-relative VC runtime
bundle location was absent; it is not accepted as a standalone distributable.
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
- At the user's request, Windows CI is being moved to GitLab while the public
  GitHub fork remains the review source. The exact GitLab pipeline and artifacts
  must be verified separately; local success is not remote CI proof.

See [the issue inventory](windows-issue-triage.md) for report-specific acceptance.

## Follow-up review corrections

QML fullscreen wrappers now call the native window manager, so keyboard and DOM
requests use its restore behavior. After correcting readiness and modifier-key
delivery in the hidden-desktop diagnostic, posted Windows F11 and Alt+Return
messages each entered fullscreen and restored the maximized window. The actual
application then exited through its native bridge with code zero, no forced
cleanup and no remaining job processes. Physical keyboard input, normal-window
restoration and geometry persistence across launches remain separate checks.

The seven native suites passed again after the QML change and geometry-helper
extraction. The logging suite now forces a same-thread Qt diagnostic during a
production rotation failure in a bounded subprocess, checks token censoring in
the fallback output, and verifies subsequent file logging.

Windows packaging now accepts explicit runtime DLL and redistributable paths;
missing required inputs fail at packaging time without blocking ordinary builds.
Two native CMake staging fixture tests passed, including paths with spaces and
missing-input failure before staging. Actual cumulative packaging remains pending.
