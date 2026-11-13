param([Parameter(Mandatory=$true)][string]$RunId)
$ErrorActionPreference = 'Stop'
$root = 'E:\CursorAI\JellyfinDesktop\deps\worktrees\integration'
$base = Join-Path $root 'deps\reconcile-20261113\gates'
$helper = Join-Path $root 'deps\w1214-proof\job_launch_prod.exe'
$sha = 'b3986ab35cca5bbd0e3a723b353daef20059e47d'
$helperHash = '7AE01DFD262A18BF863C057D570993B2C4DC529FCC03AC1E58D15F0DEDB0EBD5'
if ($RunId -notmatch '^[a-zA-Z0-9_-]{1,40}$') { throw 'Invalid run identifier' }
$run = Join-Path $base $RunId
if (Test-Path -LiteralPath $run) { throw 'Run identifier already exists' }
if ((Get-FileHash -LiteralPath $helper -Algorithm SHA256).Hash -ne $helperHash) { throw 'Helper identity mismatch' }
$python = (Get-Command python.exe -CommandType Application | Select-Object -First 1).Source
if (-not $python -or -not [IO.Path]::IsPathRooted($python)) { throw 'Absolute Python executable unavailable' }
$powershell = (Get-Command powershell.exe -CommandType Application | Select-Object -First 1).Source
if (-not $powershell -or -not [IO.Path]::IsPathRooted($powershell)) { throw 'Absolute PowerShell executable unavailable' }
Set-Location -LiteralPath $root
$head = & git.exe rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $head -ne $sha) { throw 'Source revision mismatch' }
function Check-Source {
  $actual = & git.exe -C $root rev-parse HEAD
  if ($LASTEXITCODE -ne 0 -or $actual -ne $sha) { throw 'Source revision mismatch' }
  $status = @(& git.exe -C $root status --porcelain --untracked-files=all --ignore-submodules=all)
  if ($LASTEXITCODE -ne 0 -or $status.Count) { throw 'Source status unavailable or dirty' }
  $pin = & git.exe -C $root rev-parse "${sha}:external/mpvqt"
  if ($LASTEXITCODE -ne 0) { throw 'Submodule pin unavailable' }
  $sub = Join-Path $root 'external/mpvqt'
  $actual = & git.exe -C $sub rev-parse HEAD
  if ($LASTEXITCODE -ne 0 -or $actual -ne $pin) { throw 'Submodule revision mismatch' }
  $status = @(& git.exe -C $sub status --porcelain --untracked-files=all)
  if ($LASTEXITCODE -ne 0 -or $status.Count) { throw 'Submodule status unavailable or dirty' }
  return $pin
}
$pin = Check-Source
New-Item -ItemType Directory -Path $run | Out-Null
$tests = @('test_linux_qt_pc','test_headless_routing','test_native_order','test_mac_helpers',
  'test_mac_helpers_acquire','test_macos_sdist_select_launch','test_portable_proof_assembly',
  'test_portable_proof_protocol','test_portable_proof_root_policy','test_portable_proof_supervisor',
  'test_settlement_observation','test_settlement_failure','test_settlement_candidates',
  'test_healthy_case_inventory','test_healthy_diagnostic')
$manifest = [ordered]@{ head=$head; submodule=$pin; python=$python; python_hash=(Get-FileHash $python -Algorithm SHA256).Hash;
  powershell=$powershell; helper=$helper; helper_hash=$helperHash; cases=@() }
function Invoke-Gate([string]$Name, [string]$Exe, [string[]]$Arguments) {
  if ((Get-PSDrive E).Free -lt 20GB) { throw 'Insufficient storage; stop' }
  if ((Get-FileHash -LiteralPath $helper).Hash -ne $helperHash) { throw 'Helper identity changed' }
  $case = Join-Path $run $Name
  New-Item -ItemType Directory -Path $case | Out-Null
  foreach ($dir in @('temp','local','roaming','profile')) { New-Item -ItemType Directory -Path (Join-Path $case $dir) | Out-Null }
  $env:TEMP = Join-Path $case 'temp'; $env:TMP = $env:TEMP; $env:TMPDIR = $env:TEMP
  $env:LOCALAPPDATA = Join-Path $case 'local'; $env:APPDATA = Join-Path $case 'roaming'
  $env:USERPROFILE = Join-Path $case 'profile'; $env:PYTHONDONTWRITEBYTECODE = '1'
  $receipt = Join-Path $case 'receipt.txt'
  $out = Join-Path $case 'stdout.log'; $err = Join-Path $case 'stderr.log'
  $jobArgs = @($receipt, '180000', $root, $Exe) + $Arguments
  if (@($jobArgs | Where-Object { $_ -match '"|\\$' }).Count) { throw 'Unsupported job argument' }
  $quoted = @($jobArgs | ForEach-Object { '"' + $_ + '"' })
  $proc = Start-Process -FilePath $helper -ArgumentList $quoted -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput $out -RedirectStandardError $err
  try {
    $null = $proc.Handle
    if (!$proc.WaitForExit(210000)) {
      $proc.Kill()
      if (!$proc.WaitForExit(10000)) { throw 'Launcher unsettled; job settlement unknown' }
      throw 'Launcher timed out; job settlement unknown'
    }
    $proc.WaitForExit(); $exit = $proc.ExitCode
    $actual = [ordered]@{ launcher_pid=$proc.Id; launcher_exit=$exit; executable=$Exe; arguments=$Arguments }
    $actual | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $case 'actual.json') -Encoding UTF8
    if ($exit -ne 0 -or !(Test-Path -LiteralPath $receipt -PathType Leaf)) { throw 'Launcher failed' }
    $receiptFile = Get-Item -LiteralPath $receipt -Force
    if ($receiptFile.Length -gt 1024 -or $receiptFile.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unsafe receipt' }
    $lines = @(Get-Content -LiteralPath $receipt)
    if ($lines.Count -ne 9 -or $lines[0] -cne 'receipt_version=1' -or $lines[1] -notmatch '^root_pid=[1-9][0-9]*$' -or
        $lines[2] -cne 'root_exit=0' -or $lines[3] -cne 'timeout=0 active=0' -or
        $lines[4] -notmatch '^handles_before=[0-9]+ handles_after=[0-9]+ measured=1$' -or
        $lines[5] -cne 'owned_opened=6 owned_closed=6 close_failed=0' -or $lines[6] -cne 'handles_complete=1' -or
        $lines[7] -cne 'final_result=0' -or $lines[8] -cne 'proof_complete=1') { throw 'Invalid job receipt' }
    return [ordered]@{ name=$Name; executable=$Exe; arguments=$Arguments; launcher_exit=$exit; launcher_pid=$proc.Id;
      receipt=$lines; stdout_sha256=(Get-FileHash $out).Hash; stderr_sha256=(Get-FileHash $err).Hash;
      receipt_sha256=(Get-FileHash $receipt).Hash; passed=$true }
  } finally {
    try {
      if (!$proc.HasExited) {
        $proc.Kill()
        if (!$proc.WaitForExit(10000)) { throw 'Launcher unsettled; job settlement unknown' }
        throw 'Launcher interrupted; job settlement unknown'
      }
    } finally { $proc.Dispose() }
  }
}
try {
  foreach ($test in $tests) {
    $file = "dev/ci/$test.py"
    $before = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash
    $manifest['active_case'] = [ordered]@{ name=$test; executable=$python; arguments=@('-B',$file); source_before=$before }
    $item = Invoke-Gate $test $python @('-B', $file)
    $item['source_before'] = $before; $item['source_after'] = (Get-FileHash -LiteralPath $file).Hash
    if ($item.source_before -ne $item.source_after) { throw 'Test source changed' }
    $manifest.cases += $item
    $manifest['active_case'] = $null
    if (-not $item.passed) { break }
  }
  if ($manifest.cases.Count -eq $tests.Count -and $manifest.cases[-1].passed) {
    $file = 'dev/windows/test_ci_smoke_gate.ps1'
    $before = (Get-FileHash -LiteralPath $file).Hash
    $manifest['active_case'] = [ordered]@{ name='test_ci_smoke_gate'; executable=$powershell;
      arguments=@('-NoLogo','-NoProfile','-NonInteractive','-File',$file); source_before=$before }
    $item = Invoke-Gate 'test_ci_smoke_gate' $powershell @('-NoLogo','-NoProfile','-NonInteractive','-File',$file)
    $item['source_before'] = $before; $item['source_after'] = (Get-FileHash -LiteralPath $file).Hash
    if ($item.source_before -ne $item.source_after) { throw 'Test source changed' }
    $manifest.cases += $item
    $manifest['active_case'] = $null
  }
} catch {
  $manifest['error'] = $_.Exception.Message
  throw
} finally {
  try { $manifest['final_submodule'] = Check-Source }
  catch { $manifest['final_source_error'] = $_.Exception.Message; throw }
  finally { $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $run 'manifest.json') -Encoding UTF8 }
}
if ($manifest.cases.Count -ne ($tests.Count + 1) -or @($manifest.cases | Where-Object { -not $_.passed }).Count) { exit 1 }
