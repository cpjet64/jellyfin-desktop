$ErrorActionPreference = 'Stop'
$tokens = $null
$errors = $null
$source = [Management.Automation.Language.Parser]::ParseFile("$PSScriptRoot/ci.ps1", [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw 'Cannot parse Windows CI driver' }
$function = $source.FindAll({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Run-Native' }, $true)
if ($function.Count -ne 1) { throw 'Expected one Run-Native definition' }
Invoke-Expression $function[0].Extent.Text
$gate = $source.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] -and
    $node.GetCommandName() -eq 'Run-Native' -and $node.Extent.Text -match 'test_windows_smoke_result\.mjs' }, $true)
if ($gate.Count -ne 1) { throw 'Expected one smoke-result gate' }
$script:status = 0
$script:seen = @()
function Invoke-FakeNode {
    $script:seen = @($args)
    $global:LASTEXITCODE = $script:status
}
$found = @{ Node = 'Invoke-FakeNode' }
Invoke-Expression $gate[0].Extent.Text
if (($script:seen -join '|') -ne '--test|tests/test_windows_smoke_result.mjs') { throw 'Wrong Node gate arguments' }
$script:status = 19
$failed = $false
try { Invoke-Expression $gate[0].Extent.Text }
catch { $failed = $_.Exception.Message -match 'failed with exit code 19' }
if (-not $failed) { throw 'Smoke-result failure did not propagate' }
$playerGate = $source.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] -and
    $node.GetCommandName() -eq 'Run-Native' -and $node.Extent.Text -match 'test_mpvVideoPlayer\.js' }, $true)
if ($playerGate.Count -ne 1) { throw 'Expected one video-player gate' }
$script:status = 0
Invoke-Expression $playerGate[0].Extent.Text
if (($script:seen -join '|') -ne '--test|tests/test_mpvVideoPlayer.js|tests/test_mpvVideoPlayer_order.js|tests/test_synchronous_stop_page.js|tests/test_playback_intent_acceptance.js|tests/test_recovery_plugin_acceptance.js') { throw 'Wrong video-player gate arguments' }
$script:status = 19
$failed = $false
try { Invoke-Expression $playerGate[0].Extent.Text }
catch { $failed = $_.Exception.Message -match 'failed with exit code 19' }
if (-not $failed) { throw 'Video-player failure did not propagate' }
$c2gate = $source.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] -and
    $node.GetCommandName() -eq 'Run-Native' -and $node.Extent.Text -match 'test_mpvqt_c2_patch\.py' }, $true)
if ($c2gate.Count -ne 1) { throw 'Expected one C2 patch gate' }
$venvPython = 'Invoke-FakeNode'
$script:status = 0
Invoke-Expression $c2gate[0].Extent.Text
if (($script:seen -join '|') -ne 'tests/test_mpvqt_c2_patch.py') { throw 'Wrong C2 gate arguments' }
$script:status = 19
$failed = $false
try { Invoke-Expression $c2gate[0].Extent.Text }
catch { $failed = $_.Exception.Message -match 'failed with exit code 19' }
if (-not $failed) { throw 'C2 patch failure did not propagate' }
$producerGate = $source.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] -and
    $node.GetCommandName() -eq 'Run-Native' -and $node.Extent.Text.Contains("'dev/ci/provision_jellyfin_web.py'") }, $true)
if ($producerGate.Count -ne 1) { throw 'Expected one producer contract gate' }
$script:status = 0
Invoke-Expression $producerGate[0].Extent.Text
if (($script:seen -join '|') -ne 'dev/ci/provision_jellyfin_web.py') { throw 'Wrong producer gate arguments' }
$script:status = 19
$failed = $false
try { Invoke-Expression $producerGate[0].Extent.Text }
catch { $failed = $_.Exception.Message -match 'failed with exit code 19' }
if (-not $failed) { throw 'Producer gate failure did not propagate' }
$producerTests = $source.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] -and
    $node.GetCommandName() -eq 'Run-Native' -and $node.Extent.Text.Contains('test_jellyfin_recovery_transition.cjs') }, $true)
if ($producerTests.Count -ne 1) { throw 'Expected one producer test invocation' }
$script:status = 0
Invoke-Expression $producerTests[0].Extent.Text
if (($script:seen -join '|') -ne '--test|tests/test_jellyfin_recovery_transaction.mjs|tests/test_jellyfin_recovery_manager.cjs|tests/test_jellyfin_recovery_transition.cjs') { throw 'Wrong producer test arguments' }
$script:status = 19
$failed = $false
try { Invoke-Expression $producerTests[0].Extent.Text }
catch { $failed = $_.Exception.Message -match 'failed with exit code 19' }
if (-not $failed) { throw 'Producer tests failure did not propagate' }
$copyGate = $source.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] -and
    $node.GetCommandName() -eq 'Run-Native' -and $node.Extent.Text -match 'test_fixture_producer_copy\.py' }, $true)
if ($copyGate.Count -ne 1) { throw 'Expected one fixture-copy gate' }
$script:status = 0
Invoke-Expression $copyGate[0].Extent.Text
if (($script:seen -join '|') -ne 'tests/test_fixture_producer_copy.py') { throw 'Wrong fixture-copy gate arguments' }
$script:status = 19
$failed = $false
try { Invoke-Expression $copyGate[0].Extent.Text }
catch { $failed = $_.Exception.Message -match 'failed with exit code 19' }
if (-not $failed) { throw 'Fixture-copy failure did not propagate' }
$install = $source.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] -and
    $node.GetCommandName() -eq 'Install-CiTools' }, $true)
if ($install.Count -ne 1) { throw 'Expected one tool bootstrap' }
foreach ($test in @('test_linux_sdk_order.py', 'test_linux_sdk_inputs.py', 'test_qt_sdk_archive.py',
                    'test_linux_qt_plan.py', 'test_libarchive_reader_contract.py', 'test_linux_sdk_cmake.py',
                    'test_fixture_suspend_adapter.py', 'test_fixture_suspend_linux.py', 'test_linux_containment_capability.py', 'test_linux_capability_preflight.py', 'test_linux_synthetic_service.py', 'test_settlement_candidates.py', 'test_settlement_observation.py',
                    'test_linux_synthetic_service_adapter.py', 'test_linux_synthetic_service_dbus.py',
                    'test_portable_proof_assembly.py', 'test_portable_proof_root_policy.py', 'test_macos_sdist_pins.py', 'test_provision_jellyfin_web.py', 'test_portable_proof_protocol.py',
                    'test_portable_proof_supervisor.py')) {
    $path = "dev/ci/$test"
    $matches = $source.FindAll({ param($node) $node -is [Management.Automation.Language.CommandAst] -and
        $node.GetCommandName() -eq 'Run-Native' -and $node.Extent.Text.Contains($path) }, $true)
    if ($matches.Count -ne 1 -or $matches[0].Extent.StartOffset -le $install[0].Extent.EndOffset) {
        throw "Expected mandatory post-bootstrap SDK check: $path"
    }
    $script:status = 0
    Invoke-Expression $matches[0].Extent.Text
    if (($script:seen -join '|') -ne $path) { throw "Wrong SDK check arguments: $path" }
    $script:status = 19
    $failed = $false
    try { Invoke-Expression $matches[0].Extent.Text }
    catch { $failed = $_.Exception.Message -match 'failed with exit code 19' }
    if (-not $failed) { throw "SDK check failure did not propagate: $path" }
}
Write-Host 'Windows smoke-result gate propagates native failures'
