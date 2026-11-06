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
foreach ($name in @('windows_mpv_runtime', 'healthy_graph_arm', 'healthy_graph_failure_provenance', 'healthy_graph_capture', 'healthy_graph_capture_lifetime')) {
    if ($name -notin $build) { throw "Missing target $name" }
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
