$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
Set-Location $root
$build = Join-Path $root 'build'
$deps = Join-Path $root 'deps'
$ci = Join-Path $deps "ci-$env:CI_JOB_ID"
$results = Join-Path $build 'ci-results'
$qtVersion = '6.9.3'
$mpvVersion = '20261018-git-01e2533f31'
. "$PSScriptRoot/ci-tools.ps1"
. "$PSScriptRoot/ci-mpv.ps1"
. "$PSScriptRoot/ci-gui.ps1"
. "$PSScriptRoot/ci-mpvqt.ps1"
. "$PSScriptRoot/ci-preflight.ps1"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Run-Native {
    param([string]$Program, [string[]]$Arguments)
    $held = @('test_renderer_loss_offscreen', 'test_mpris_active_stop', 'test_c2_retirement_native')
    if ([IO.Path]::GetFileNameWithoutExtension($Program) -in $held) {
        throw 'BLOCKED: native render fixture violates the render-thread contract'
    }
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}

function Invoke-HealthyCopyPreflight {
    param([string]$Python, [string]$Root, [string]$Scratch)
    $keys = @('MPVQT_TEST_SOURCE', 'HEALTHY_TEST_SOURCE', 'CMAKE_GENERATOR', 'CMAKE_GENERATOR_PLATFORM',
              'CMAKE_GENERATOR_TOOLSET', 'CMAKE_GENERATOR_INSTANCE', 'TEMP', 'TMP', 'TMPDIR')
    $previous = @{}
    foreach ($key in $keys) { $previous[$key] = [Environment]::GetEnvironmentVariable($key, 'Process') }
    try {
        $env:MPVQT_TEST_SOURCE = Join-Path $Root 'external/mpvqt'
        $env:HEALTHY_TEST_SOURCE = $Root
        $env:CMAKE_GENERATOR = 'Ninja'
        foreach ($key in @('CMAKE_GENERATOR_PLATFORM', 'CMAKE_GENERATOR_TOOLSET', 'CMAKE_GENERATOR_INSTANCE')) {
            [Environment]::SetEnvironmentVariable($key, $null, 'Process')
        }
        $env:TEMP = $Scratch; $env:TMP = $Scratch; $env:TMPDIR = $Scratch
        Run-Native $Python @('tests/test_healthy_restart_acceptance.py')
    } finally {
        foreach ($key in $keys) { [Environment]::SetEnvironmentVariable($key, $previous[$key], 'Process') }
    }
}

if ($env:CI_JOB_ID -notmatch '^\d+$') { throw 'This script requires an isolated GitLab job checkout.' }
$drive = Get-PSDrive -Name ([System.IO.Path]::GetPathRoot($root).Substring(0, 1))
Write-NativeInventory $root $ci $env:CI_MPVQT_MODE
if ($env:CI_JOB_IMAGE) { throw 'BLOCKED: native shell runner required' }
if (-not [Environment]::Is64BitOperatingSystem -or $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') { throw 'BLOCKED: Windows x64 runner required' }
$mode = if ($env:CI_MPVQT_MODE) { $env:CI_MPVQT_MODE } else { 'static' }
if ($mode -notin @('static', 'system')) { throw 'CI_MPVQT_MODE must be static or system' }
$sourceSha = & git.exe -C external/mpvqt rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $sourceSha -ne '2ebbd38bf65d01be9c55d673ecd43c8bb9a6fc8e') { throw 'Unexpected mpvqt source revision' }
if ($drive.Free -lt 20GB) { throw 'At least 20 GiB free space is required before dependency downloads.' }
$python = Find-OptionalTool 'python.exe'
if (-not $python) { throw 'Python is required on the runner' }
if (-not (Find-OptionalTool 'curl.exe')) { throw 'Windows curl.exe is required for mpv downloads' }
$found = @{
    SevenZip = Find-OptionalTool '7z.exe' @("$env:ProgramFiles/7-Zip/7z.exe")
    CMake = Find-OptionalTool 'cmake.exe'
    Ninja = Find-OptionalTool 'ninja.exe'
    Node = Find-OptionalTool 'node.exe'
    Inno = Find-OptionalTool 'ISCC.exe' @("${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe", "$env:ProgramFiles/Inno Setup 6/ISCC.exe")
}
if ($found.Inno) {
    # ISCC's file-version metadata can be 0.0.0.0. Reuse only the tested compiler.
    if ((Get-FileHash -LiteralPath $found.Inno -Algorithm SHA256).Hash -ne '0A8757031B33777E4C9CBFFEE40F11A5062B36D25CBE144C1DB73B6102B80AD7') {
        $found.Inno = $null
    }
}
if ($found.Node) {
    $nodeVersion = & $found.Node --version
    if ($LASTEXITCODE -ne 0 -or $nodeVersion -notmatch '^v(\d+)\.' -or [int]$Matches[1] -lt 20) {
        $found.Node = $null
    }
}
if ($found.CMake) {
    $ctest = Join-Path (Split-Path $found.CMake) 'ctest.exe'
    $versionLine = & $found.CMake --version | Select-Object -First 1
    $minimumCmake = if ($mode -eq 'system') { [version]'3.30' } else { [version]'3.21' }
    if ($LASTEXITCODE -ne 0 -or $versionLine -notmatch 'cmake version (\d+\.\d+)' -or
        [version]$Matches[1] -lt $minimumCmake -or -not (Test-Path -LiteralPath $ctest)) {
        $found.CMake = $null
    }
}
if ((@($found.Values | Where-Object { -not $_ }).Count -gt 0) -and -not (Find-OptionalTool 'tar.exe')) {
    throw 'Windows tar.exe is required to bootstrap missing tools'
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'vswhere not found' }
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'MSVC x64 tools not found' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$redist = Get-ChildItem -Path "$vs/VC/Redist/MSVC/*/x64/Microsoft.VC*.CRT" -Directory -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $redist) { throw 'MSVC x64 redistributable DLLs not found' }
foreach ($name in @('PATH', 'INCLUDE', 'LIB', 'LIBPATH')) {
    $value = & cmd.exe /d /v:on /c "call `"$vcvars`" >nul && echo !$name!"
    if ($LASTEXITCODE -ne 0 -or -not $value) { throw "MSVC environment setup failed for $name" }
    [Environment]::SetEnvironmentVariable($name, ($value -join "`n"), 'Process')
}
New-Item -ItemType Directory -Force -Path $ci, $build, $results | Out-Null
Install-CiTools "$ci/tools" $found
Invoke-HealthyCopyPreflight $python $root $ci
$sevenZip = $found.SevenZip
Install-Mpv 'x86_64-v3' "$build/mpv" $sevenZip $ci $mpvVersion '49533E80D681672E42AB38FCCFDB9DB0CC9FC423F86D34F5F9634EBA71109912'
Install-Mpv 'x86_64' "$build/mpv-fallback" $sevenZip $ci $mpvVersion 'D7F5621F42B6E161FE032DF914A10BDECE28A89DF3E57C6C3E16A5A250929B5E'
Run-Native $python @('-m', 'venv', "$ci/venv")
$venvPython = Join-Path $ci 'venv/Scripts/python.exe'
Run-Native $venvPython @('tests/test_ci_results.py')
Run-Native $venvPython @('tests/test_windows_loader_driver.py')
Run-Native $venvPython @('dev/ci/test_provision_jellyfin_web.py')
Run-Native $venvPython @('dev/ci/test_evidence.py')
Run-Native $venvPython @('dev/ci/test_fixture_suspend_policy.py')
Run-Native $venvPython @('dev/ci/test_fixture_suspend_adapter.py')
Run-Native $venvPython @('dev/ci/test_fixture_suspend_linux.py')
Run-Native $venvPython @('dev/ci/test_linux_containment_capability.py')
Run-Native $venvPython @('dev/ci/test_linux_capability_preflight.py')
Run-Native $venvPython @('dev/ci/test_linux_synthetic_service.py')
Run-Native $venvPython @('dev/ci/test_linux_synthetic_service_adapter.py')
Run-Native $venvPython @('dev/ci/test_linux_synthetic_service_dbus.py')
Run-Native $venvPython @('dev/ci/provision_dbus_next.py')
Run-Native $venvPython @('dev/ci/test_linux_synthetic_service_real_dbus.py')
Run-Native $venvPython @('dev/ci/test_settlement_failure.py')
Run-Native $venvPython @('dev/ci/test_settlement_candidates.py')
Run-Native $venvPython @('dev/ci/test_settlement_observation.py')
Run-Native $venvPython @('dev/ci/test_build_manifest.py')
Run-Native $venvPython @('dev/ci/test_acceptance.py')
Run-Native $venvPython @('dev/ci/test_safe_extract.py')
Run-Native $venvPython @('dev/ci/test_macos_session.py')
Run-Native $venvPython @('dev/ci/test_mac_helpers.py')
Run-Native $venvPython @('dev/ci/test_qga_terminal.py')
Run-Native $venvPython @('dev/ci/test_portable_proof_assembly.py')
Run-Native $venvPython @('dev/ci/test_portable_proof_root_policy.py')
Run-Native $venvPython @('dev/ci/test_portable_proof_protocol.py')
Run-Native $venvPython @('dev/ci/test_portable_proof_supervisor.py')
Run-Native $venvPython @('dev/ci/test_macos_sdist_static.py')
Run-Native $venvPython @('dev/ci/test_macos_sdist_select.py')
Run-Native $venvPython @('dev/ci/test_macos_sdist_supervise.py')
Run-Native $venvPython @('dev/ci/test_macos_sdist_select_launch.py')
Run-Native $venvPython @('dev/ci/test_macos_sdist_pins.py')
Run-Native $venvPython @('dev/ci/test_linux_qt_cache.py')
Run-Native $venvPython @('dev/ci/test_mac_carchive.py')
Run-Native $venvPython @('dev/ci/test_mac_helpers_acquire.py')
Run-Native $venvPython @('dev/ci/test_linux_sdk_order.py')
Run-Native $venvPython @('dev/ci/test_linux_sdk_inputs.py')
Run-Native $venvPython @('dev/ci/test_qt_sdk_archive.py')
Run-Native $venvPython @('dev/ci/test_linux_qt_plan.py')
Run-Native $venvPython @('dev/ci/test_linux_qt_pc.py')
Run-Native $venvPython @('dev/ci/test_libarchive_reader_contract.py')
Run-Native $venvPython @('dev/ci/test_linux_sdk_cmake.py')
Run-Native $venvPython @('tests/test_windows_mpv_runtime_configure.py')
Run-Native 'powershell.exe' @('-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', 'dev/windows/test_ci_gui.ps1')
Run-Native $venvPython @('-m', 'pip', 'install', 'aqtinstall==3.3.0')
Run-Native $venvPython @('-m', 'aqt', 'install-qt', 'windows', 'desktop', $qtVersion, 'win64_msvc2022_64', '-m', 'qtwebengine', 'qtwebchannel', 'qtpositioning', '-O', "$ci/qt")
$exports = & dumpbin.exe /exports "$build/mpv/libmpv-2.dll"
if ($LASTEXITCODE -ne 0) { throw 'dumpbin failed' }
$names = @($exports | ForEach-Object { if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(mpv_\w+)\s*$') { $Matches[1] } })
if ($names.Count -eq 0) { throw 'No mpv exports found' }
@('LIBRARY libmpv-2.dll', 'EXPORTS') + $names | Set-Content -LiteralPath "$build/mpv/mpv.def" -Encoding ASCII
Run-Native 'lib.exe' @("/def:$build/mpv/mpv.def", "/out:$build/mpv/libmpv-2.dll.lib", '/MACHINE:X64')
New-Item -ItemType Directory -Force -Path "$deps/vcruntime" | Out-Null
Copy-Item -Path "$($redist.FullName)/*.dll" -Destination "$deps/vcruntime"
Invoke-WebRequest -UseBasicParsing 'https://aka.ms/vs/17/release/vc_redist.x64.exe' -OutFile "$deps/vc_redist.x64.exe"
$qt = "$ci/qt/$qtVersion/msvc2022_64"
$env:PATH = "$qt/bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$qt/plugins"
$static = if ($mode -eq 'static') { 'ON' } else { 'OFF' }
$configure = @('-S', $root, '-B', $build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=RelWithDebInfo', "-DCMAKE_INSTALL_PREFIX=$build/output", "-DQTROOT=$qt", "-DMPV_INCLUDE_DIR=$build/mpv/include", "-DMPV_LIBRARY=$build/mpv/libmpv-2.dll.lib", '-DCHECK_FOR_UPDATES=ON', "-DUSE_STATIC_MPVQT=$static", "-DMPVQT_EXTERNAL_EVENTS=$static")
if ($mode -eq 'system') {
    Install-JobMpvQt $ci $qt "$build/mpv/include" "$build/mpv/libmpv-2.dll.lib" $venvPython
    $mpvqtPrefix = "$ci/system-mpvqt/mpvqt-install"
    $configure += @("-DCMAKE_PREFIX_PATH=$qt;$mpvqtPrefix", "-DMpvQt_DIR=$script:JobMpvQtConfigDirectory", "-DLibmpv_INCLUDE_DIRS=$build/mpv/include", "-DLibmpv_LIBRARIES=$build/mpv/libmpv-2.dll.lib")
} else {
    $configure += "-DCMAKE_PREFIX_PATH=$qt"
}
Run-Native 'cmake.exe' $configure
Run-Native 'cmake.exe' @('--build', $build, '--parallel', '4')
$env:CI_EVIDENCE_EXECUTABLE = "$build/tests/test_renderer_loss_offscreen.exe"
Run-Native $venvPython @('dev/ci/build_manifest.py')
if ($mode -eq 'system') {
    & dumpbin.exe /dependents "$build/src/Jellyfin Desktop.exe" | Set-Content "$results/system-mpvqt-dependents.txt"
    if ($LASTEXITCODE -ne 0) { throw 'Unable to inspect system MpvQt linkage' }
    if (-not (Select-String -LiteralPath "$results/system-mpvqt-dependents.txt" -Pattern 'MpvQt.*\.dll')) { throw 'Expected shared MpvQt linkage is missing' }
    Write-Host "Verified job-local system MpvQt runtime: $script:JobMpvQtRuntime"
}
$testHome = Join-Path $build "ci-test-home-$env:CI_JOB_ID"
if (Test-Path -LiteralPath $testHome) { throw 'Test runtime directory already exists' }
New-Item -ItemType Directory -Path "$testHome/roaming", "$testHome/local" -Force | Out-Null
$oldAppData = $env:APPDATA
$oldLocalAppData = $env:LOCALAPPDATA
$oldOwnedRoot = $env:JELLYFIN_TEST_OWNED_ROOT
try {
$env:APPDATA = "$testHome/roaming"
$env:LOCALAPPDATA = "$testHome/local"
$env:JELLYFIN_TEST_OWNED_ROOT = $build
Run-Native 'ctest.exe' @('--test-dir', $build, '-LE', 'gui', '--no-tests=error', '--output-on-failure', '--timeout', '60', '--output-junit', "$results/headless-ctest.xml")
$reportArgs = @('dev/ci/check_results.py', 'junit', "$results/headless-ctest.xml")
$required = @('test_fault_stop', 'test_webclient_transport', 'test_webclient_fault_guard', 'test_mpv_test_bootstrap', 'test_mpv_test_bootstrap_pure', 'test_smtc_admission_model', 'test_c2a_hold_gate', 'test_c2_error_terminal', 'test_c2_fault_quiescence_policy', 'test_c2_synthetic_service', 'test_c2_fault_restore_permission', 'test_c2_fault_restore_frame', 'test_c2_capture_observation', 'test_c2_quiescence_observation', 'test_player_intent_observation', 'test_synchronous_stop_trace', 'test_synchronous_stop_events', 'test_healthy_cleanup_coordinator', 'test_healthy_wake_binding', 'test_playback_intent_acceptance', 'test_playback_checkpoint', 'test_player_intent_boundary', 'test_recovery_bridge_session', 'test_recovery_bridge_host', 'test_windows_loader_mode', 'test_playback_native_receipts', 'test_player_native_receipts', 'test_native_observation', 'test_player_native_restore', 'test_fixture_restart_broker')
if ($mode -eq 'static') { $required += @('test_mpvqt_event_ownership', 'test_mpvqt_player_binding', 'test_player_released_state') }
$required += @('test_fixture_frame_acceptance', 'test_fixture_frame_capture', 'test_windows_restart_io', 'test_windows_restart_process', 'test_windows_restart_channel')
$required += @('test_player_restore_cleanup', 'test_player_restore_observation')
$required += @('test_fixture_restart_wire', 'test_fixture_restart_child', 'test_restart_child_player', 'test_windows_frame_reader')
$required += @('test_windows_broker_operations', 'test_restart_source_player')
$required += @('test_healthy_retirement_lifetime', 'test_healthy_retirement_races', 'test_healthy_retirement_qt')
$required += @('test_healthy_retirement_owner', 'test_healthy_retirement_owner_context')
$required += @('test_healthy_module_identity')
$required += @('test_healthy_closure', 'test_healthy_closure_wake', 'test_healthy_async_stop')
$required += @('test_healthy_module_gate_wire', 'test_healthy_module_gate_import', 'test_healthy_module_gate_factory')
$required += @('test_healthy_graph_retirement', 'test_healthy_graph_retirement_guard', 'test_healthy_graph_retirement_access')
$required += @('test_healthy_identity_commitment', 'test_healthy_module_gate_endpoints')
$required += @('test_healthy_paused_hold', 'test_healthy_resume_progress', 'test_restart_closed_wire', 'test_healthy_module_launch')
$required += @('test_healthy_child_session', 'test_healthy_child_session_source')
$required += @('test_restart_cancel_wire', 'test_restart_child_cancel', 'test_healthy_capture_capability')
$required += @('test_healthy_child_session_cancel', 'test_healthy_child_session_grant_io')
$required += @('test_verified_cancel_gate')
$required += @('test_windows_restart_io_guard_cancel')
$required += @('test_healthy_cleanup_claim', 'test_healthy_child_session_failure')
foreach ($name in $required) { $reportArgs += @('--require', $name) }
Run-Native $venvPython $reportArgs
Run-Native $venvPython @('dev/ci/evidence.py', 'build_core', 'passed', 'Native build and core tests passed')
Run-Native $venvPython @('dev/ci/evidence.py', 'offscreen', 'blocked', 'Native render-thread contract is unresolved; fixture held before launch')
Run-Native $venvPython @('tests/windows_mpv_loader_integration.py')
Run-Native $venvPython @('tests/windows_packaging_runtime_test.py')
$env:ISCC = $found.Inno
$env:WINDOWS_VCRUNTIME_DIR = "$deps/vcruntime"
$env:WINDOWS_VCREDIST_EXE = "$deps/vc_redist.x64.exe"
Run-Native $venvPython @('tests/windows_installer_compile_test.py')
Run-Native $venvPython @('tests/test_bundle_paths.py')
Run-Native $venvPython @('tests/test_windows_package_freshness.py')
$previousMpvQtTestSource = $env:MPVQT_TEST_SOURCE
try {
    $env:MPVQT_TEST_SOURCE = "$root/external/mpvqt"
    Run-Native $venvPython @('tests/test_mpvqt_shared_patch.py')
    Run-Native $venvPython @('tests/test_mpvqt_c2_patch.py')
    Run-Native $venvPython @('tests/test_fixture_producer_copy.py')
} finally {
    $env:MPVQT_TEST_SOURCE = $previousMpvQtTestSource
}
Run-Native $found.Node @('--test', 'tests/test_inputPlugin.js')
Run-Native $found.Node @('--test', 'tests/test_mpvVideoPlayer.js', 'tests/test_mpvVideoPlayer_order.js', 'tests/test_synchronous_stop_page.js', 'tests/test_playback_intent_acceptance.js', 'tests/test_recovery_plugin_acceptance.js')
Run-Native $venvPython @('dev/ci/provision_jellyfin_web.py')
$previousJellyfinWebSource = [Environment]::GetEnvironmentVariable('JELLYFIN_WEB_SOURCE', 'Process')
try {
    [Environment]::SetEnvironmentVariable('JELLYFIN_WEB_SOURCE', "$root/deps/ci-jellyfin-web/playbackmanager.js", 'Process')
    Run-Native $found.Node @('--test', 'tests/test_jellyfin_recovery_transaction.mjs', 'tests/test_jellyfin_recovery_manager.cjs', 'tests/test_jellyfin_recovery_transition.cjs')
} finally {
    [Environment]::SetEnvironmentVariable('JELLYFIN_WEB_SOURCE', $previousJellyfinWebSource, 'Process')
}
Run-Native $found.Node @('--test', 'tests/test_windows_smoke_result.mjs')
Run-Native $found.Node @('tests/windows_hidden_desktop_fixture.mjs')
} finally {
    $env:APPDATA = $oldAppData
    $env:LOCALAPPDATA = $oldLocalAppData
    $env:JELLYFIN_TEST_OWNED_ROOT = $oldOwnedRoot
    Remove-Item -LiteralPath $testHome -Recurse -Force
}
Run-Native $venvPython @('dev/windows/package.py', $build, '--parallel', '4')
$artifacts = @(Get-ChildItem -Path "$build/JellyfinDesktop-*.exe", "$build/JellyfinDesktop-*.zip")
if (@($artifacts | Where-Object Extension -eq '.exe').Count -ne 1 -or @($artifacts | Where-Object Extension -eq '.zip').Count -ne 1) { throw 'Expected exactly one installer and one portable ZIP' }
$artifacts | Get-FileHash -Algorithm SHA256 | ForEach-Object { "$($_.Hash)  $($_.Path | Split-Path -Leaf)" } | Set-Content "$results/SHA256SUMS.txt"
@("commit=$env:CI_COMMIT_SHA", "pipeline=$env:CI_PIPELINE_ID", "job=$env:CI_JOB_ID", "qt=$qtVersion", "mpv=$mpvVersion") | Set-Content "$results/provenance.txt"
