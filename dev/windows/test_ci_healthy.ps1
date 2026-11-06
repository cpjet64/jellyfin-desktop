$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/ci-healthy.ps1"
$script:calls = @()
$script:fail = ''
$script:missing = ''
$script:cmakeVersion = '3.21.0'
function Test-Path { param([string]$LiteralPath) return $LiteralPath -notmatch 'diagnostic-build|healthy-diagnostic-ctest.xml' -and $LiteralPath -ne $script:missing }
function New-Item { param([string]$ItemType, [string]$Path) return $Path }
function cmake.exe { $global:LASTEXITCODE = 0; return "cmake version $script:cmakeVersion" }
function Run-Native {
    param([string]$Program, [string[]]$Arguments)
    $script:calls += ,@($Program, $Arguments)
    if ($Program -eq 'ctest.exe' -and $env:QT_QPA_PLATFORM -ne 'offscreen') { throw 'Diagnostic test must be offscreen' }
    if ($Program -eq 'ctest.exe' -and $env:JELLYFIN_TEST_OWNED_ROOT -ne (Join-Path 'diagnostic-build' 'owned-fixtures')) { throw 'Wrong fixture root' }
    $phase = if ($Program -eq 'ctest.exe') { 'test' } elseif ($Program -eq 'python.exe') { 'report' }
             elseif ($Arguments[0] -eq '--build') { 'build' } else { 'configure' }
    if ($phase -eq $script:fail) { throw "injected $phase failure" }
}
function Invoke-Case {
    param([string]$Fail = '', [string]$Missing = '')
    $script:calls = @(); $script:fail = $Fail; $script:missing = $Missing
    Invoke-HealthyDiagnostic 'root' 'diagnostic-build' 'qt' 'mpv' 'python.exe' 'results'
}
function Expect-Failure {
    param([string]$Fail = '', [string]$Missing = '')
    $threw = $false
    try { Invoke-Case $Fail $Missing } catch { $threw = $true }
    if (-not $threw) { throw 'Expected a diagnostic failure' }
}
$env:QT_QPA_PLATFORM = 'previous-platform'
$env:JELLYFIN_TEST_OWNED_ROOT = 'previous-root'
Invoke-Case
if ($env:QT_QPA_PLATFORM -ne 'previous-platform') { throw 'Diagnostic platform was not restored' }
if ($env:JELLYFIN_TEST_OWNED_ROOT -ne 'previous-root') { throw 'Fixture root was not restored' }
if ($script:calls.Count -ne 4) { throw 'Expected configure, build, test and report' }
if ($script:calls[0][1][0] -ne '-S' -or $script:calls[1][1][0] -ne '--build' -or
    $script:calls[2][0] -ne 'ctest.exe' -or $script:calls[3][0] -ne 'python.exe') { throw 'Wrong diagnostic order' }
$configure = $script:calls[0][1]
foreach ($flag in @('-DUSE_STATIC_MPVQT=ON', '-DMPVQT_EXTERNAL_EVENTS=ON',
    '-DPLAYER_INTENT_DIAGNOSTIC_ONLY=ON', '-DHEALTHY_RESTART_DIAGNOSTIC_ONLY=ON')) {
    if ($flag -notin $configure) { throw "Missing $flag" }
}
$build = $script:calls[1][1]; $test = $script:calls[2][1]; $report = $script:calls[3][1]
$expected = @('healthy_async_stop_player', 'healthy_unpause_player', 'healthy_fixture_adapter_cleanup',
    'healthy_healthy_restore_cancel', 'healthy_async_stop_source', 'healthy_player_native_receipts',
    'healthy_player_restore_cleanup', 'healthy_player_restore_observation', 'healthy_player_native_restore',
    'healthy_graph_capture', 'healthy_graph_capture_lifetime', 'healthy_graph_arm', 'healthy_graph_failure_provenance',
    'healthy_graph_dispatch', 'healthy_b_channel_dispatch')
$targetStart = [array]::IndexOf($build, '--target') + 1
$targetEnd = [array]::IndexOf($build, '--parallel') - 1
if ($targetStart -lt 1 -or $targetEnd -lt $targetStart) { throw 'Invalid build target arguments' }
$targets = @($build[$targetStart..$targetEnd])
$expectedTargets = @('windows_mpv_runtime') + $expected
if ($targets.Count -ne 16 -or (($targets | Sort-Object) -join '|') -cne (($expectedTargets | Sort-Object) -join '|')) {
    throw 'Diagnostic build targets differ from the approved inventory'
}
$regexIndex = [array]::IndexOf($test, '-R')
if ($regexIndex -lt 0 -or $regexIndex + 1 -ge $test.Count) { throw 'Missing CTest selection' }
$expectedRegex = '^(' + (($expected | Sort-Object | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')$'
$actualRegex = $test[$regexIndex + 1]
if ($actualRegex -notmatch '^\^\((.*)\)\$$') { throw 'CTest selection is not anchored' }
$actualNames = @($Matches[1] -split '\|' | Sort-Object)
$sortedRegex = '^(' + ($actualNames -join '|') + ')$'
if ($actualNames.Count -ne 15 -or $sortedRegex -cne $expectedRegex) { throw 'CTest selection differs from the approved inventory' }
$reportNames = @($report | Select-Object -Skip 2)
if ($reportNames.Count -ne 15 -or (($reportNames | Sort-Object) -join '|') -cne (($expected | Sort-Object) -join '|')) {
    throw 'JUnit requirements differ from the approved inventory'
}
if ('--no-tests=error' -notin $test -or '--output-junit' -notin $test -or
    'healthy_graph_capture' -notin $report -or 'healthy_graph_capture_lifetime' -notin $report -or
    'healthy_graph_failure_provenance' -notin $report) { throw 'Incomplete diagnostic report contract' }
if (($test -join ' ') -match 'healthy_mpvqt_event_ownership|test_healthy_restart') { throw 'Held test selected' }
foreach ($phase in @('configure', 'build', 'test', 'report')) { Expect-Failure $phase }
if ($env:QT_QPA_PLATFORM -ne 'previous-platform' -or $env:JELLYFIN_TEST_OWNED_ROOT -ne 'previous-root') { throw 'Failure did not restore environment' }
Expect-Failure '' 'qt'
$script:cmakeVersion = '3.20.0'
Expect-Failure
if ($script:calls.Count -ne 0) { throw 'Old CMake reached the build' }
Write-Host 'Windows diagnostic routing checks passed'
