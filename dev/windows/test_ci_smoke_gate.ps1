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
Write-Host 'Windows smoke-result gate propagates native failures'
