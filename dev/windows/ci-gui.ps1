function Run-HiddenTest {
    param([string]$Helper, [string]$Executable, [string]$Evidence, [string]$Python, [string]$DiagnosticExe = '')
    New-Item -ItemType Directory -Force -Path $Evidence | Out-Null
    & $Helper $Executable $Evidence '--test-executable'
    $testExit = $LASTEXITCODE
    if ($testExit -ne 0) {
        $report = Join-Path $Evidence 'qtest.txt'
        $marker = 'Video ready timed out; see startup-result\.json|Scene graph reported an error; see startup-result\.json'
        if ($DiagnosticExe -and (Test-Path -LiteralPath $report) -and (Select-String -LiteralPath $report -Pattern $marker -Quiet)) {
            if (Test-Path -LiteralPath $DiagnosticExe -PathType Leaf) {
                try {
                    & $Helper $DiagnosticExe $Evidence '--gl-probe'
                    Write-Host "Renderer diagnostic exit: $LASTEXITCODE; original test remains failed"
                } catch { Write-Host 'Renderer diagnostic could not run; original test remains failed' }
            } else { Write-Host 'Renderer diagnostic executable missing; original test remains failed' }
        }
        throw "Hidden desktop test failed: $Executable ($testExit)"
    }
    if (-not (Test-Path -LiteralPath "$Evidence/qtest.txt" -PathType Leaf)) {
        throw "Hidden desktop test did not produce a result: $Executable"
    }
    Run-Native $Python @('dev/ci/check_results.py', 'qtest', "$Evidence/qtest.txt")
}

function Invoke-NativeGuiTests {
    param([string]$Build, [string]$Results, [string]$Python)
    Invoke-NativeGuiSuite $Build $Results $Python
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
            $diagnostic = if ($fixture -eq 'test_renderer_loss_playback') { "$Build/tests/native_gl_probe.exe" } else { '' }
            Run-HiddenTest $helper "$Build/tests/$fixture.exe" $evidence $Python $diagnostic
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
