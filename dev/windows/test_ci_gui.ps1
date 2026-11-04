$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/ci-gui.ps1"
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$scratch = Join-Path $root "ci-gui-helper-test-$PID"
if (Test-Path -LiteralPath $scratch) { throw 'Test directory already exists' }
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
$script:diagnosticCalls = 0
$script:fixtureText = ''
$script:helperCalls = 0
$script:nativeCalls = 0
$script:allowFixture = $false
function Run-Native { param($Program, $Arguments) $script:nativeCalls++ }
function fake-helper {
    param($Executable, $Evidence, $Mode)
    $script:helperCalls++
    if ($Mode -eq '--gl-probe') { $script:diagnosticCalls++; $global:LASTEXITCODE = 0; return }
    $script:fixtureText | Set-Content -LiteralPath "$Evidence/qtest.txt"
    $global:LASTEXITCODE = if ($script:allowFixture) { 0 } else { 7 }
}
try {
    foreach ($name in @('test_renderer_loss_offscreen', 'test_mpris_active_stop', 'test_c2_retirement_native')) {
        $evidence = Join-Path $scratch "held-$name"
        foreach ($executable in @("$name.exe", "C:/somewhere/$name.EXE", "C:\somewhere\$name.exe")) {
            $blocked = $false
            try { Run-HiddenTest 'fake-helper' $executable $evidence 'unused' }
            catch { $blocked = $_.Exception.Message -match 'render-thread contract' }
            if (-not $blocked -or (Test-Path -LiteralPath $evidence)) { throw "Held fixture was not rejected: $executable" }
            $blocked = $false
            try { Run-HiddenTest 'fake-helper' 'test_webclient_fault_webengine.exe' $evidence 'unused' $executable }
            catch { $blocked = $_.Exception.Message -match 'render-thread contract' }
            if (-not $blocked -or (Test-Path -LiteralPath $evidence) -or $script:helperCalls -or $script:nativeCalls) {
                throw "Held diagnostic was not rejected before launch: $executable"
            }
        }
    }
    $blocked = $false
    $offscreen = Join-Path $scratch 'held-suite'
    try { Invoke-OffscreenTests $offscreen $offscreen 'unused' }
    catch { $blocked = $_.Exception.Message -match 'render-thread contract' }
    if (-not $blocked -or (Test-Path -LiteralPath $offscreen) -or $script:helperCalls -or $script:nativeCalls) {
        throw 'Held offscreen suite launched a process or created evidence'
    }
    $script:allowFixture = $true
    Run-HiddenTest 'fake-helper' 'test_webclient_fault_webengine.exe' (Join-Path $scratch 'allowed') 'unused'
    if ($script:helperCalls -ne 1 -or $script:nativeCalls -ne 1) { throw 'Permitted fixture control did not run' }
    $script:allowFixture = $false
    $probe = Join-Path $scratch 'native_gl_probe.exe'
    New-Item -ItemType File -Path $probe | Out-Null
    foreach ($case in @(
        @{ Text = 'Video ready timed out; see startup-result.json'; Calls = 1 },
        @{ Text = 'Scene graph reported an error; see startup-result.json'; Calls = 2 },
        @{ Text = 'Unrelated failure'; Calls = 2 }
    )) {
        $script:fixtureText = $case.Text
        $failed = $false
        try { Run-HiddenTest 'fake-helper' 'fixture.exe' $scratch 'unused' $probe }
        catch { $failed = $_.Exception.Message -match 'Hidden desktop test failed.*\(7\)' }
        if (-not $failed -or $script:diagnosticCalls -ne $case.Calls) { throw 'Diagnostic trigger or original failure was not preserved' }
    }
    Write-Host 'Diagnostic trigger and preserved failure checks passed'
} finally {
    Remove-Item -LiteralPath $scratch -Recurse -Force
}
