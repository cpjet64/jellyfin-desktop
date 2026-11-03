$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/ci-gui.ps1"
$root = (Resolve-Path "$PSScriptRoot/../..").Path
$scratch = Join-Path $root "deps/ci-gui-helper-test-$PID"
if (Test-Path -LiteralPath $scratch) { throw 'Test directory already exists' }
New-Item -ItemType Directory -Path $scratch -Force | Out-Null
$script:diagnosticCalls = 0
$script:fixtureText = ''
function fake-helper {
    param($Executable, $Evidence, $Mode)
    if ($Mode -eq '--gl-probe') { $script:diagnosticCalls++; $global:LASTEXITCODE = 0; return }
    $script:fixtureText | Set-Content -LiteralPath "$Evidence/qtest.txt"
    $global:LASTEXITCODE = 7
}
try {
    $probe = Join-Path $scratch 'probe.exe'
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
