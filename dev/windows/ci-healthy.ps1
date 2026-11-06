function Invoke-HealthyDiagnostic {
    param([string]$Root, [string]$Build, [string]$Qt, [string]$Mpv,
          [string]$Python, [string]$Results)
    $tests = @('healthy_async_stop_player', 'healthy_unpause_player', 'healthy_fixture_adapter_cleanup',
        'healthy_healthy_restore_cancel', 'healthy_async_stop_source', 'healthy_player_native_receipts',
        'healthy_player_restore_cleanup', 'healthy_player_restore_observation', 'healthy_player_native_restore',
        'healthy_graph_arm', 'healthy_graph_failure_provenance', 'healthy_graph_capture', 'healthy_graph_capture_lifetime',
        'healthy_graph_dispatch', 'healthy_b_channel_dispatch')
    foreach ($path in @($Qt, "$Mpv/include", "$Mpv/libmpv-2.dll.lib", "$Mpv/libmpv-2.dll")) {
        if (-not (Test-Path -LiteralPath $path)) { throw "Missing diagnostic prerequisite: $path" }
    }
    if (Test-Path -LiteralPath $Build) { throw 'Diagnostic build directory already exists' }
    $version = & cmake.exe --version | Select-Object -First 1
    if ($LASTEXITCODE -ne 0 -or $version -notmatch 'cmake version (\d+\.\d+)' -or
        [version]$Matches[1] -lt [version]'3.21') { throw 'Diagnostic tests require CMake 3.21 or newer' }
    $configure = @('-S', $Root, '-B', $Build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
        "-DQTROOT=$Qt", "-DCMAKE_PREFIX_PATH=$Qt", "-DMPV_INCLUDE_DIR=$Mpv/include",
        "-DMPV_LIBRARY=$Mpv/libmpv-2.dll.lib", '-DUSE_STATIC_MPVQT=ON', '-DMPVQT_EXTERNAL_EVENTS=ON',
        '-DPLAYER_INTENT_DIAGNOSTIC_ONLY=ON', '-DHEALTHY_RESTART_DIAGNOSTIC_ONLY=ON')
    Run-Native 'cmake.exe' $configure
    Run-Native 'cmake.exe' (@('--build', $Build, '--target', 'windows_mpv_runtime') + $tests + @('--parallel', '4'))
    $report = Join-Path $Results 'healthy-diagnostic-ctest.xml'
    if (Test-Path -LiteralPath $report) { throw 'Diagnostic report already exists' }
    $regex = '^(' + (($tests | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')$'
    $previousPlatform = $env:QT_QPA_PLATFORM
    $previousOwnedRoot = $env:JELLYFIN_TEST_OWNED_ROOT
    $ownedRoot = Join-Path $Build 'owned-fixtures'
    if (Test-Path -LiteralPath $ownedRoot) { throw 'Diagnostic fixture directory already exists' }
    New-Item -ItemType Directory -Path $ownedRoot | Out-Null
    try {
        $env:QT_QPA_PLATFORM = 'offscreen'
        $env:JELLYFIN_TEST_OWNED_ROOT = $ownedRoot
        Run-Native 'ctest.exe' @('--test-dir', $Build, '-R', $regex, '--no-tests=error',
            '--output-on-failure', '--timeout', '60', '--output-junit', $report)
    } finally {
        $env:QT_QPA_PLATFORM = $previousPlatform
        $env:JELLYFIN_TEST_OWNED_ROOT = $previousOwnedRoot
    }
    Run-Native $Python (@('dev/windows/check_healthy_results.py', $report) + $tests)
}
