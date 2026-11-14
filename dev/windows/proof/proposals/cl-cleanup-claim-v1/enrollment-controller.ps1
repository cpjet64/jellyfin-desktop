param([Parameter(Mandatory)][string]$ReviewedSourceSHA,
      [Parameter(Mandatory)][string]$ControllerSHA256)
$ErrorActionPreference = 'Stop'
$policyRepo = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-build-policy-20261113'
$qualified = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-fixture-detached-20261114'
$proposal = "$policyRepo/dev/windows/proof/proposals/cl-cleanup-claim-v1"
$root = "$policyRepo/deps/enrollment-cl-claim-20261114-01"
$helper = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/integration/deps/w1214-proof/job_launch_prod.exe'
$python = 'C:/Python314/python.exe'
$envFile = 'E:/CursorAI/JellyfinDesktop/deps/reconcile-20261113/build-metadata-01/env-private/environment-private.json'
$evidence = 'E:/CursorAI/JellyfinDesktop/deps/reconcile-20261113/meta-claim-01/result.json'
$manifest = "$qualified/deps/fixture-detached-v1/qualification-manifest.json"
$interop = "$proposal/RetainedInputs.cs"
$driver = "$proposal/enrollment-driver.py"
if ($ReviewedSourceSHA -cnotmatch '^[0-9a-f]{40}$' -or $ControllerSHA256 -cnotmatch '^[0-9a-f]{64}$' -or
    (Get-FileHash -LiteralPath $PSCommandPath).Hash.ToLowerInvariant() -cne $ControllerSHA256) { throw 'Controller identity mismatch' }

function Check-Worktree($path, $sha) {
  $head = & git.exe -C $path rev-parse HEAD
  if ($LASTEXITCODE -ne 0 -or $head -cne $sha) { throw 'Worktree HEAD mismatch' }
  $status = @(& git.exe -C $path status --porcelain --untracked-files=all --ignore-submodules=none)
  if ($LASTEXITCODE -ne 0 -or $status.Count) { throw 'Worktree status unavailable or dirty' }
}
function Check-Sources {
  Check-Worktree $policyRepo $ReviewedSourceSHA
  Check-Worktree $qualified 'd0bb016df169b663567e88fd5586ab7c008e9554'
  Check-Worktree 'E:/CursorAI/JellyfinDesktop/deps/worktrees/automatic-recovery-review' '4453818fcdeae3152fd07f29031ec6f3eb3b1670'
  Check-Worktree 'E:/CursorAI/JellyfinDesktop/deps/worktrees/integration' 'ff831ccc7832d147f77dc4e69006ccda0190fa89'
  foreach ($name in @('enrollment-driver.py','enrollment-controller.ps1','RetainedInputs.cs','cleanup-claim.rsp')) {
    $relative = 'dev/windows/proof/proposals/cl-cleanup-claim-v1/' + $name
    $blob = & git.exe -C $policyRepo -c core.autocrlf=false hash-object --no-filters -- "$proposal/$name"
    if ($LASTEXITCODE -ne 0) { throw 'Raw source blob unavailable' }
    $tracked = & git.exe -C $policyRepo rev-parse "${ReviewedSourceSHA}:$relative"
    if ($LASTEXITCODE -ne 0 -or $blob -cne $tracked) { throw 'Tracked source mismatch' }
  }
}
function Check-Receipt($path) {
  $lines = @(Get-Content -LiteralPath $path)
  if ($lines.Count -ne 9 -or $lines[0] -cne 'receipt_version=1' -or $lines[1] -notmatch '^root_pid=[1-9][0-9]*$' -or
      $lines[2] -cne 'root_exit=0' -or $lines[3] -cne 'timeout=0 active=0' -or
      $lines[4] -notmatch '^handles_before=[0-9]+ handles_after=[0-9]+ measured=[01]$' -or
      $lines[5] -cne 'owned_opened=6 owned_closed=6 close_failed=0' -or $lines[6] -cne 'handles_complete=1' -or
      $lines[7] -cne 'final_result=0' -or $lines[8] -cne 'proof_complete=1') { throw 'Outer receipt nonpass' }
  return $lines
}
function Assert-PrivateAcl {
  $user = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
  $allowed = @($user,'S-1-5-18','S-1-5-32-544')
  foreach ($path in @([IO.Path]::GetDirectoryName($envFile),$envFile)) {
    $acl = Get-Acl -LiteralPath $path
    if ($acl.GetOwner([Security.Principal.SecurityIdentifier]).Value -cne $user) { throw 'Private owner mismatch' }
    $rules = @($acl.GetAccessRules($true,$true,[Security.Principal.SecurityIdentifier]))
    if ($rules.Count -ne 3) { throw 'Private ACL mismatch' }
    foreach ($rule in $rules) {
      if ($rule.IdentityReference.Value -cnotin $allowed -or $rule.AccessControlType -ne 'Allow' -or
          ($rule.FileSystemRights -band [Security.AccessControl.FileSystemRights]::FullControl) -ne [Security.AccessControl.FileSystemRights]::FullControl) { throw 'Private ACL mismatch' }
    }
  }
}
function Pin-Inputs($pins) {
  $pins.PinFile($driver,'a0156d0bfdb84fc91a5d031b5a7d5a5986fd76a5f5f712d760cbecb8e2a805c9',(Get-Item -LiteralPath $driver).Length,$null)
  $pins.PinFile($interop,'76c97bc07ada9c4f8335a23b4eb12ded531a83b72c11d78eda9a85a39934cbaa',(Get-Item -LiteralPath $interop).Length,$null)
  $pins.PinFile($PSCommandPath,$ControllerSHA256,(Get-Item -LiteralPath $PSCommandPath).Length,$null)
  $pins.PinFile($python,'7ca24f26d6e3f463419ee4f537ddd3acd312c38fe45e678cce08572f26a8bd1a',106328,$null)
  $pins.PinFile($helper,'7ae01dfd262a18bf863c057d570993b2c4dc529fcc03ac1e58d15f0dedb0ebd5',166400,$null)
  $pins.PinFile($envFile,'1daa8974e1d37694f1067c92ab9839bab22d2f8a1d3fa844e0b2b58ea0c4512a',(Get-Item -LiteralPath $envFile).Length,$null)
  $pins.PinFile($evidence,'672c7ae1298a59b7d37d7595cd7dd7f33fc5c3c90a591498e0ca6820464be020',(Get-Item -LiteralPath $evidence).Length,$null)
  $pins.PinFile($manifest,'dbe55eb7c529d11386700a60dab96f56ab12bc7b3c74e6b0e55277fe6ca4b7e0',(Get-Item -LiteralPath $manifest).Length,$null)
  $m = Get-Content -Raw -LiteralPath $manifest | ConvertFrom-Json
  if ($m.source_sha -cne 'd0bb016df169b663567e88fd5586ab7c008e9554' -or $m.internal_sha256 -cne '80b842ae0d7ad4c3384b9eacfb9c492962febebae449c90a2b18d6b1e4f25915' -or @($m.files).Count -ne 21) { throw 'Qualified manifest mismatch' }
  foreach ($entry in $m.files) { $pins.PinFile((Join-Path $qualified $entry.path),$entry.sha256,$entry.bytes,$null) }
  $r = Get-Content -Raw -LiteralPath $evidence | ConvertFrom-Json
  if ($r.identity.private_environment_digest -cne 'd9b37c10d4dd5d21ec86562737d8aea7dce17216e56733ae819b5dcfc4f3af01') { throw 'Environment observation mismatch' }
  foreach ($item in @($r.identity.compiler,$r.identity.telemetry) + @($r.identity.direct_inputs) + @($r.identity.support_tools)) {
    $id = ($item.file_id -join ':')
    $pins.PinFile($item.path,$item.sha256,$item.size,$id)
  }
  $pins.PinFile("$proposal/cleanup-claim.rsp",'15d4a13278b38de63943cc4fc8776cbec79d44e4759a968d927ceb65f77de347',3129,$null)
  $pins.PinParents($root)
}
function Assert-ScratchParent {
  $parent = [IO.DirectoryInfo]::new([IO.Path]::GetDirectoryName($root))
  while ($null -ne $parent) {
    if (!$parent.Exists -or ($parent.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unsafe scratch parent' }
    $parent = $parent.Parent
  }
}
function Get-FailurePostchecks($pins) {
  $state = [ordered]@{pins='unavailable';source='unknown';private_acl='unknown';outer_receipt='unavailable'}
  if ($null -ne $pins) {
    try { $pins.VerifyAll(); $state.pins = 'pass' } catch { $state.pins = 'failed' }
  }
  try { Check-Sources; $state.source = 'pass' } catch { $state.source = 'failed' }
  try { Assert-PrivateAcl; $state.private_acl = 'pass' } catch { $state.private_acl = 'failed' }
  if (Test-Path -LiteralPath "$root/outer.receipt" -PathType Leaf) {
    try { $null = Check-Receipt "$root/outer.receipt"; $state.outer_receipt = 'strict_success' }
    catch { $state.outer_receipt = 'nonpass_or_unreadable' }
  }
  return $state
}
function Test-StorageFailure($failure) {
  $pattern = '(?i)no space left|disk full|not enough (space|storage)|quota exceeded|errno 28|winerror 112|0x80070070'
  if ($failure.ToString() -match $pattern) { return $true }
  foreach ($name in @('outer.stdout','outer.stderr','compiler.stdout','compiler.stderr')) {
    $path = Join-Path $root $name
    if (Test-Path -LiteralPath $path -PathType Leaf) {
      if ((Get-Item -LiteralPath $path).Length -gt 4MB) { return $true }
      foreach ($line in [IO.File]::ReadLines($path)) { if ($line -match $pattern) { return $true } }
    }
  }
  return $false
}
function Invoke-CompileHelper {
  $argsList = @("$root/outer.receipt",'180000',"$qualified/dev/windows/proof",$python,'-B',$driver)
  $psi = [Diagnostics.ProcessStartInfo]::new()
  $psi.FileName = $helper; $psi.WorkingDirectory = "$qualified/dev/windows/proof"
  $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true; $psi.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
  $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
  foreach ($value in $argsList) { $psi.ArgumentList.Add($value) }
  $null = $psi.Environment.Remove('PYTHONPATH'); $null = $psi.Environment.Remove('PYTHONHOME')
  $psi.Environment['PYTHONDONTWRITEBYTECODE'] = '1'
  $out = [IO.File]::Open("$root/outer.stdout",[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
  try {
    $err = [IO.File]::Open("$root/outer.stderr",[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try {
      $proc = [Diagnostics.Process]::new(); $proc.StartInfo = $psi; $watch = [Diagnostics.Stopwatch]::StartNew()
      try {
        if (!$proc.Start()) { throw 'Helper did not start' }
        $helperPid = $proc.Id; $null = $proc.Handle
        $outTask = $proc.StandardOutput.BaseStream.CopyToAsync($out); $errTask = $proc.StandardError.BaseStream.CopyToAsync($err)
        $finished = $false
        while ($watch.ElapsedMilliseconds -lt 200000) {
          if ($outTask.IsFaulted -or $errTask.IsFaulted) { throw 'Log drain failed; stop' }
          if ($proc.WaitForExit([Math]::Min(200,200000-[int]$watch.ElapsedMilliseconds))) { $finished = $true; break }
        }
        if (!$finished) { throw 'Helper timeout; settlement unknown' }
        if (![Threading.Tasks.Task]::WaitAll(@($outTask,$errTask),[Math]::Max(0,210000-[int]$watch.ElapsedMilliseconds))) { throw 'Log drains unsettled' }
        $exit = $proc.ExitCode; $out.Flush($true); $err.Flush($true)
      } finally {
        try {
          if (!$proc.HasExited) {
            $proc.Kill()
            if (!$proc.WaitForExit([Math]::Max(0,210000-[int]$watch.ElapsedMilliseconds))) { throw 'Helper unsettled' }
          }
        } finally { $proc.Dispose() }
      }
    } finally { $err.Dispose() }
  } finally { $out.Dispose() }
  if (Test-StorageFailure '') { throw 'Storage failure or oversized log; preserve evidence and stop' }
  if ($exit -ne 0) { throw 'Helper nonpass; retain evidence' }
  return @{pid=$helperPid;exit=$exit;elapsed=$watch.ElapsedMilliseconds;argv=$argsList}
}
function Read-DiscoveryResult($pins, $run) {
  $receipt = @(Check-Receipt "$root/outer.receipt")
  $pins.VerifyAll()
  Check-Sources
  Assert-PrivateAcl
  $build = Get-Content -Raw -LiteralPath "$root/build.receipt" | ConvertFrom-Json
  if ($build.schema -cne 'build-only-v1' -or $build.result -cnotin @('natural','telemetry_cleanup') -or
      $build.root.exit -ne 0 -or !$build.root.signaled -or !$build.root.closed -or $build.final.active -ne 0 -or
      $build.owned.opened -ne $build.owned.closed -or $build.owned.failed -ne 0) { throw 'Build receipt nonpass' }
  $dependencies = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
  foreach ($line in [IO.File]::ReadLines("$root/compiler.stdout")) {
    if ($line -match '^\s*Note: including file:\s*(.+?)\s*$') {
      $path = [IO.Path]::GetFullPath($Matches[1])
      if (!$dependencies.ContainsKey($path)) { $dependencies.Add($path,$pins.ObserveFile($path)) }
    }
  }
  if ($dependencies.Count -eq 0) { throw 'No parseable showIncludes observations; retain raw output' }
  $pins.VerifyAll()
  $inventory = @(Get-ChildItem -LiteralPath $root -File -Recurse -Force | ForEach-Object {
    if ($_.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Output reparse point' }
    [ordered]@{path=[IO.Path]::GetRelativePath($root,$_.FullName);bytes=$_.Length;
      sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
  })
  return [ordered]@{schema='cleanup-claim-discovery-v1';source=$ReviewedSourceSHA;
    helper_pid=$run.pid;helper_exit=$run.exit;elapsed_ms=$run.elapsed;argv=$run.argv;
    outer_receipt=$receipt;build_result=$build.result;files=$inventory;postcompile_dependency_identities=$dependencies;
    limitations='Discovery only; generated include tree, implicit CWD behavior and dependency closure remain unqualified'}
}
Check-Sources
Assert-PrivateAcl
if ((Get-PSDrive E).Free -lt 20GB) { throw 'Insufficient storage; stop' }
if (Test-Path -LiteralPath $root) { throw 'Output root is not fresh' }
Assert-ScratchParent
New-Item -ItemType Directory -Path $root | Out-Null
$hostScratch = Join-Path $root 'host-temp'
New-Item -ItemType Directory -Path $hostScratch | Out-Null
$previousTemp = [Environment]::GetEnvironmentVariable('TEMP','Process')
$previousTmp = [Environment]::GetEnvironmentVariable('TMP','Process')
[Environment]::SetEnvironmentVariable('TEMP',$hostScratch,'Process')
[Environment]::SetEnvironmentVariable('TMP',$hostScratch,'Process')
$pins = $null
try {
$csStream = [IO.File]::Open($interop,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
try {
  $digest = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($csStream)).ToLowerInvariant()
  if ($digest -cne '76c97bc07ada9c4f8335a23b4eb12ded531a83b72c11d78eda9a85a39934cbaa') { throw 'Interop identity mismatch' }
  $csStream.Position = 0
  $reader = [IO.StreamReader]::new($csStream)
  $source = $reader.ReadToEnd()
  Add-Type -TypeDefinition $source
} finally { $csStream.Dispose() }
$pins = [RetainedInputs]::new()
$primary = $null
try {
  Pin-Inputs $pins
  $pins.PinParents("$root/placeholder")
  Check-Sources
  foreach ($name in @('temp','roaming','local')) { New-Item -ItemType Directory -Path (Join-Path $root $name) | Out-Null }
  $run = Invoke-CompileHelper
  $record = Read-DiscoveryResult $pins $run
  $successBytes = [Text.UTF8Encoding]::new($false).GetBytes((ConvertTo-Json -InputObject $record -Depth 8) + "`n")
} catch {
  $primary = $_
  $postchecks = Get-FailurePostchecks $pins
  $storageFailure = $true
  try { $storageFailure = Test-StorageFailure $primary } catch { $storageFailure = $true }
  if (!$storageFailure) {
    try {
      $failure = [ordered]@{schema='cleanup-claim-discovery-failed-v1';source=$ReviewedSourceSHA;
        reason=$primary.ToString();postchecks=$postchecks;settlement='see retained outer receipt; no success implied'}
      $data = [Text.UTF8Encoding]::new($false).GetBytes((ConvertTo-Json -InputObject $failure -Depth 5) + "`n")
      $failureFile = [IO.File]::Open("$root/failure.json",[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
      try { $failureFile.Write($data,0,$data.Length); $failureFile.Flush($true) } finally { $failureFile.Dispose() }
    } catch { Write-Warning 'Failure evidence write failed; original error retained' }
  }
  throw $primary
} finally {
  if ($null -ne $pins) {
    try { $pins.Dispose() } catch {
      if ($null -eq $primary) { throw }
      Write-Warning 'Retained handle close failed; original error retained'
    }
  }
}
$summary = [IO.File]::Open("$root/result.json",[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try { $summary.Write($successBytes,0,$successBytes.Length); $summary.Flush($true) } finally { $summary.Dispose() }
Write-Output "Discovery compile settled; helper=$($run.pid); source=$ReviewedSourceSHA; dependencies require review"
} finally {
  [Environment]::SetEnvironmentVariable('TEMP',$previousTemp,'Process')
  [Environment]::SetEnvironmentVariable('TMP',$previousTmp,'Process')
}
