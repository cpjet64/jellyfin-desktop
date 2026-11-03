function Run-HiddenTest {
    param([string]$Helper, [string]$Executable, [string]$Evidence, [string]$Python)
    New-Item -ItemType Directory -Force -Path $Evidence | Out-Null
    & $Helper $Executable $Evidence '--test-executable'
    if ($LASTEXITCODE -ne 0) { throw "Hidden desktop test failed: $Executable ($LASTEXITCODE)" }
    if (-not (Test-Path -LiteralPath "$Evidence/qtest.txt" -PathType Leaf)) {
        throw "Hidden desktop test did not produce a result: $Executable"
    }
    Run-Native $Python @('dev/ci/check_results.py', 'qtest', "$Evidence/qtest.txt")
}

function Invoke-NativeGuiTests {
    param([string]$Build, [string]$Results, [string]$Python)
    $scratch = Join-Path $Build "ci-gui-runtime-$env:CI_JOB_ID"
    if (Test-Path -LiteralPath $scratch) { throw 'GUI runtime directory already exists' }
    $oldAppData = $env:APPDATA
    $oldLocalAppData = $env:LOCALAPPDATA
    New-Item -ItemType Directory -Path "$scratch/roaming", "$scratch/local" -Force | Out-Null
    try {
        $env:APPDATA = "$scratch/roaming"
        $env:LOCALAPPDATA = "$scratch/local"
        Invoke-NativeGuiSuite $Build $Results $Python
    } finally {
        $env:APPDATA = $oldAppData
        $env:LOCALAPPDATA = $oldLocalAppData
        Remove-Item -LiteralPath $scratch -Recurse -Force
    }
}

function Invoke-NativeGuiSuite {
    param([string]$Build, [string]$Results, [string]$Python)
    $helper = Join-Path $Build 'hidden-desktop-ci.exe'
    Run-Native 'cl.exe' @('/nologo', '/std:c++14', '/EHsc', 'tests/windows_hidden_desktop.cpp', "/Fe:$helper", "/Fo:$Build/hidden-desktop-ci.obj", 'user32.lib')
    $env:QSG_INFO = '1'
    $env:QT_QPA_PLATFORM = 'windows'
    $media = Join-Path $Results 'recovery.y4m'
    Run-Native $Python @('tests/generate_recovery_media.py', $media)
    Run-HiddenTest $helper "$Build/tests/test_webclient_fault_webengine.exe" "$Results/webengine" $Python
    foreach ($fixture in @('test_renderer_loss_playback', 'test_webclient_containment')) {
        foreach ($scenario in @('playing', 'paused', 'held-hook')) {
            $evidence = Join-Path $Results "$fixture/$scenario"
            $env:RECOVERY_SCENARIO = $scenario
            $env:RECOVERY_MEDIA = $media
            $env:RECOVERY_EVIDENCE_DIR = $evidence
            Run-HiddenTest $helper "$Build/tests/$fixture.exe" $evidence $Python
            if ($fixture -eq 'test_renderer_loss_playback' -and $scenario -ne 'held-hook') {
                foreach ($image in @('before.png', 'after.png')) {
                    if (-not (Test-Path -LiteralPath "$evidence/$image" -PathType Leaf)) { throw "Missing $scenario image $image" }
                }
            }
            if ($fixture -eq 'test_webclient_containment' -and $scenario -ne 'held-hook') {
                foreach ($image in @('containment-before.png', 'containment-after.png')) {
                    if (-not (Test-Path -LiteralPath "$evidence/$image" -PathType Leaf)) { throw "Missing containment $scenario image $image" }
                }
            }
        }
    }
    Write-Host 'PASS: WebEngine and all 6 playback scenarios on hidden desktops'
}
