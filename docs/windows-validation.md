# Windows repair validation — 2026-11-03

Baseline: upstream `a96ee9dfc2fee7090b644283b2de767b5ee7b9e8`.
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
attributes are tested; protected-directory ACL behavior and an affected CPU are not.
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

## Outstanding acceptance

- A full pre-change application baseline build/launch was not captured; no
  before/after real-playback claim is made.
- Real Windows UI, WebEngine backend, server connection, representative playback,
  stop, clean exit, and relaunch need an authorized test account and media.
- Production watch-history changes were not assumed authorized.
- Reboot persistence, display rearrangement, HDR/capture and mixed-GPU cases,
  sustained memory growth, and affected non-AVX2 CPU compatibility remain open.
- No upstream issue was closed, and no release, signing, or deployment was performed.
- GitHub workflow results must be checked for the exact pushed SHA separately;
  local success is not remote CI proof.

See [the issue inventory](windows-issue-triage.md) for report-specific acceptance.
