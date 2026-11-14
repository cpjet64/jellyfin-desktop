[CmdletBinding()]
param([switch]$Delete)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$env:GIT_OPTIONAL_LOCKS = '0'
$root = 'E:\CursorAI\JellyfinDesktop'
$verifier = Join-Path $root 'deps\reconcile-20261113\cleanup\verify.py'
$log = Join-Path $root 'deps\reconcile-20261113\cleanup\removals.jsonl'
$names = @(
    'c2a-fixtures', 'w1021-continuation', 'w1021-reservation', 'w1021-wiring',
    'w796-restart-broker', 'w904-closure', 'w929-identity-commitment',
    'w931-broker-closed', 'w939-windows-tests', 'w965-final', 'w965-session-wiring',
    'w981-adapter-tests', 'w997-coherent', 'w425-mac-helpers', 'w915-closure'
)

function Assert-NoProjectActivity {
    $buildNames = @('cmake.exe', 'ninja.exe', 'ctest.exe', 'msbuild.exe', 'cl.exe', 'link.exe', 'qmake.exe')
    $processes = @(Get-CimInstance Win32_Process -ErrorAction Stop)
    foreach ($process in $processes) {
        if ($process.ProcessId -eq $PID) { continue }
        $image = [string]$process.ExecutablePath
        $command = [string]$process.CommandLine
        if ($process.Name -in $buildNames -or
            $image.IndexOf($root, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or
            $command.IndexOf($root, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or
            $command.IndexOf($root.Replace('\', '/'), [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            throw "Active project or build process found: PID $($process.ProcessId), $($process.Name)"
        }
    }
}

function Assert-Verified([string[]]$Remaining) {
    & python $verifier @Remaining
    if ($LASTEXITCODE -ne 0) { throw "Read-only verification failed: $LASTEXITCODE" }
}

function Write-RemovalLog([string]$Name, [string]$State, [string]$Detail) {
    $entry = @{ utc = [DateTime]::UtcNow.ToString('o'); worktree = $Name; state = $State; detail = $Detail }
    [IO.File]::AppendAllText($log, (($entry | ConvertTo-Json -Compress) + [Environment]::NewLine))
}

Assert-NoProjectActivity
Assert-Verified $names
if (-not $Delete) {
    Write-Output 'Verification passed. No worktrees were removed.'
    return
}

$remaining = [Collections.Generic.List[string]]::new()
foreach ($name in $names) { $remaining.Add($name) }
foreach ($name in $names) {
    Assert-NoProjectActivity
    Assert-Verified $remaining.ToArray()
    $path = Join-Path (Join-Path $root 'deps\worktrees') $name
    if (-not (Test-Path -LiteralPath $path -PathType Container)) { throw "Missing target: $path" }
    Write-RemovalLog $name 'starting' 'Verified exact target before git worktree remove.'
    & git -C $root worktree remove --force -- $path
    if ($LASTEXITCODE -ne 0) {
        Write-RemovalLog $name 'failed' "git returned $LASTEXITCODE"
        throw "Removal failed for $name; stopped."
    }
    $remaining.Remove($name) | Out-Null
    try {
        Assert-Verified $remaining.ToArray()
        Write-RemovalLog $name 'removed' 'Registration and physical absence verified.'
    } catch {
        Write-RemovalLog $name 'verification_failed' $_.Exception.Message
        throw
    }
}
