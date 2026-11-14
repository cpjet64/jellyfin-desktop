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
$builder = "$proposal/enrollment_policy.py"
$postcheck = "$proposal/enrollment-postcheck.py"
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
  foreach ($name in @('enrollment-driver.py','enrollment_policy.py','enrollment-postcheck.py','enrollment-controller.ps1','RetainedInputs.cs','cleanup-claim.rsp')) {
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
  $pins.PinFile($driver,'b46274e2b5fc541a27093c870f49160f8235b0d8d2497ceaa546799d45dcc90e',(Get-Item -LiteralPath $driver).Length,$null)
  $pins.PinFile($builder,'19dec0f9289936fb60c974c970a8e7ddacbce1671b7be9d03f9d41eeed52fe8a',(Get-Item -LiteralPath $builder).Length,$null)
  $pins.PinFile($postcheck,'506b7ab9ec55546262d737698b84cd1d5cf7c07655d682f2ac2523dd84bd0088',(Get-Item -LiteralPath $postcheck).Length,$null)
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
    try { $pins.VerifyAll(); $state.pins = 'pass' } catch { $state.pins = 'failed'; Add-CleanupError 'pin postcheck' $_ }
  }
  try { Check-Sources; $state.source = 'pass' } catch { $state.source = 'failed'; Add-CleanupError 'source postcheck' $_ }
  try { Assert-PrivateAcl; $state.private_acl = 'pass' } catch { $state.private_acl = 'failed'; Add-CleanupError 'ACL postcheck' $_ }
  if (Test-Path -LiteralPath "$root/outer.receipt" -PathType Leaf) {
    try { $null = Check-Receipt "$root/outer.receipt"; $state.outer_receipt = 'strict_success' }
    catch { $state.outer_receipt = 'nonpass_or_unreadable' }
  }
  return $state
}
function Test-StorageFailure($failure) {
  $pattern = '(?i)no space left|disk full|not enough (space|storage)|quota exceeded|errno 28|winerror 112|0x80070070'
  if ($failure.ToString() -match $pattern) { return $true }
  $pending = [Collections.Generic.Stack[Exception]]::new()
  if ($failure.Exception -is [Exception]) { $pending.Push($failure.Exception) }
  $seen = 0
  while ($pending.Count) {
    if (++$seen -gt 100) { return $true }
    $errorObject = $pending.Pop()
    if (($errorObject.HResult -band 0xffff) -in @(39,112,1816) -or $errorObject.Message -match $pattern) { return $true }
    if ($errorObject -is [AggregateException]) {
      foreach ($inner in $errorObject.InnerExceptions) { $pending.Push($inner) }
    } elseif ($null -ne $errorObject.InnerException) { $pending.Push($errorObject.InnerException) }
  }
  foreach ($name in @('outer.stdout','outer.stderr','compiler.stdout','compiler.stderr','postcheck.stdout','postcheck.stderr')) {
    $path = Join-Path $root $name
    if (Test-Path -LiteralPath $path -PathType Leaf) {
      if ((Get-Item -LiteralPath $path).Length -gt 4MB) { return $true }
      foreach ($line in [IO.File]::ReadLines($path)) { if ($line -match $pattern) { return $true } }
    }
  }
  return $false
}
function Add-CleanupError($stage, $errorRecord) {
  $script:audit.secondary.Add("$stage : $($errorRecord.ToString())")
  try { if (Test-StorageFailure $errorRecord) { $script:audit.suppress = $true } }
  catch { $script:audit.suppress = $true }
}
function Close-Helper($proc, $started, $watch, $deadline) {
  if ($null -eq $proc) { return }
  if ($started) {
    try {
      if (!$proc.HasExited) {
        try { $proc.Kill() } catch { Add-CleanupError 'helper kill' $_ }
        try {
          if (!$proc.WaitForExit([Math]::Max(0,$deadline-[int]$watch.ElapsedMilliseconds))) {
            throw 'Helper unsettled after cleanup deadline'
          }
        } catch { Add-CleanupError 'helper wait' $_ }
      }
    } catch { Add-CleanupError 'helper state' $_ }
  }
  try { $proc.Dispose() } catch { Add-CleanupError 'helper dispose' $_ }
}
function Invoke-FixedHelper($phase, $driverExit) {
  if ($phase -ceq 'compile') {
    $argsList = @("$root/outer.receipt",'180000',"$qualified/dev/windows/proof",$python,'-B',$driver)
    $stdout = "$root/outer.stdout"; $stderr = "$root/outer.stderr"; $wait = 200000; $deadline = 210000
  } elseif ($phase -ceq 'postcheck') {
    $argsList = @("$root/postcheck.receipt",'30000',"$qualified/dev/windows/proof",$python,'-B',$postcheck,[string]$driverExit)
    $stdout = "$root/postcheck.stdout"; $stderr = "$root/postcheck.stderr"; $wait = 35000; $deadline = 40000
  } else { throw 'Unknown fixed helper phase' }
  $psi = [Diagnostics.ProcessStartInfo]::new()
  $psi.FileName = $helper; $psi.WorkingDirectory = "$qualified/dev/windows/proof"
  $psi.UseShellExecute = $false; $psi.CreateNoWindow = $true; $psi.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
  $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
  foreach ($value in $argsList) { $psi.ArgumentList.Add($value) }
  $null = $psi.Environment.Remove('PYTHONPATH'); $null = $psi.Environment.Remove('PYTHONHOME')
  $psi.Environment['PYTHONDONTWRITEBYTECODE'] = '1'
  $out = $null; $err = $null; $proc = $null; $started = $false; $failure = $null
  $watch = [Diagnostics.Stopwatch]::StartNew()
  try {
    $out = [IO.File]::Open($stdout,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    $err = [IO.File]::Open($stderr,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    $proc = [Diagnostics.Process]::new(); $proc.StartInfo = $psi
    if (!$proc.Start()) { throw 'Helper did not start' }
    $started = $true; $script:audit.started = $true; $helperPid = $proc.Id; $null = $proc.Handle
    $outTask = $proc.StandardOutput.BaseStream.CopyToAsync($out); $errTask = $proc.StandardError.BaseStream.CopyToAsync($err)
    $finished = $false
    while ($watch.ElapsedMilliseconds -lt $wait) {
      if ($outTask.IsFaulted -or $errTask.IsFaulted) {
        $script:audit.suppress = $true
        if ($outTask.IsFaulted) { throw $outTask.Exception }
        throw $errTask.Exception
      }
      if ($proc.WaitForExit([Math]::Min(200,$wait-[int]$watch.ElapsedMilliseconds))) { $finished = $true; break }
    }
    if (!$finished) { throw 'Helper timeout; settlement unknown' }
    try {
      if (![Threading.Tasks.Task]::WaitAll(@($outTask,$errTask),[Math]::Max(0,$deadline-[int]$watch.ElapsedMilliseconds))) {
        throw 'Log drains unsettled'
      }
    } catch { $script:audit.suppress = $true; throw }
    $exit = $proc.ExitCode; $out.Flush($true); $err.Flush($true)
  } catch { $failure = $_ }
  finally {
    Close-Helper $proc $started $watch $deadline
    if ($null -ne $err) { try { $err.Dispose() } catch { Add-CleanupError 'stderr close' $_ } }
    if ($null -ne $out) { try { $out.Dispose() } catch { Add-CleanupError 'stdout close' $_ } }
  }
  if ($null -ne $failure) { throw $failure }
  if ($script:audit.secondary.Count) { throw 'Helper cleanup failed' }
  if (Test-StorageFailure '') { throw 'Storage failure or oversized log; preserve evidence and stop' }
  if ($exit -ne 0) { throw 'Helper nonpass; retain evidence' }
  return @{pid=$helperPid;exit=$exit;elapsed=$watch.ElapsedMilliseconds;argv=$argsList}
}
function Get-ScratchInventory {
  $pending = [Collections.Generic.Stack[string]]::new()
  $pending.Push($root)
  $files = [Collections.Generic.List[object]]::new()
  while ($pending.Count -gt 0) {
    $directory = $pending.Pop()
    foreach ($item in Get-ChildItem -LiteralPath $directory -Force) {
      if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Scratch reparse point' }
      if ($item.PSIsContainer) { $pending.Push($item.FullName); continue }
      $files.Add([ordered]@{path=[IO.Path]::GetRelativePath($root,$item.FullName);bytes=$item.Length;
        sha256=(Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()})
    }
  }
  return ,$files.ToArray()
}
function Read-DiscoveryResult($pins, $run, $postrun, $receipt, $postreceipt) {
  $pins.VerifyAll()
  Check-Sources
  Assert-PrivateAcl
  $validated = Get-Content -Raw -LiteralPath "$root/postcheck.stdout" | ConvertFrom-Json
  if ($validated.schema -cne 'enrollment-postcheck-v1' -or $validated.result -cne 'VALIDATED' -or
      $validated.receipt_handles.opened -ne 1 -or $validated.receipt_handles.closed -ne 1 -or
      $validated.receipt_handles.failed -ne 0 -or @($validated.outputs).Count -ne 2) { throw 'Receipt postcheck nonpass' }
  $dependencies = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
  foreach ($line in [IO.File]::ReadLines("$root/compiler.stdout")) {
    if ($line -match '^\s*Note: including file:\s*(.+?)\s*$') {
      $path = [IO.Path]::GetFullPath($Matches[1])
      if (!$dependencies.ContainsKey($path)) { $dependencies.Add($path,$pins.ObserveFile($path)) }
    }
  }
  if ($dependencies.Count -eq 0) { throw 'No parseable showIncludes observations; retain raw output' }
  $pins.VerifyAll()
  $inventory = @(Get-ScratchInventory)
  foreach ($output in $validated.outputs) {
    $relative = [IO.Path]::GetRelativePath($root,$output.path)
    $entry = @($inventory | Where-Object { $_.path -ceq $relative })
    if ($entry.Count -ne 1 -or $entry[0].bytes -ne $output.size -or $entry[0].sha256 -cne $output.sha256) {
      throw 'Validated output differs from actual inventory'
    }
  }
  return [ordered]@{schema='cleanup-claim-discovery-v1';source=$ReviewedSourceSHA;
    helper_pid=$run.pid;helper_exit=$run.exit;elapsed_ms=$run.elapsed;argv=$run.argv;
    outer_receipt=$receipt;postcheck_helper_pid=$postrun.pid;postcheck_helper_exit=$postrun.exit;
    postcheck_receipt=$postreceipt;validated_receipt=$validated;files=$inventory;postcompile_dependency_identities=$dependencies;
    limitations='Discovery only; generated include tree, implicit CWD behavior and dependency closure remain unqualified'}
}
function Initialize-Scratch {
  Check-Sources; Assert-PrivateAcl
  if ((Get-PSDrive E).Free -lt 20GB) { throw 'Insufficient storage; stop' }
  if (Test-Path -LiteralPath $root) { throw 'Output root is not fresh' }
  Assert-ScratchParent
  New-Item -ItemType Directory -Path $root | Out-Null
  $script:audit.rootCreated = $true
  $scratch = Join-Path $root 'host-temp'
  New-Item -ItemType Directory -Path $scratch | Out-Null
  $script:audit.temp = [Environment]::GetEnvironmentVariable('TEMP','Process')
  $script:audit.tmp = [Environment]::GetEnvironmentVariable('TMP','Process')
  $script:audit.envSaved = $true
  [Environment]::SetEnvironmentVariable('TEMP',$scratch,'Process')
  [Environment]::SetEnvironmentVariable('TMP',$scratch,'Process')
}
function Load-RetainedInterop {
  if ($PSVersionTable.PSEdition -cne 'Core' -or $PSVersionTable.PSVersion.Major -ne 7 -or
      [IO.Path]::GetFileName([Environment]::ProcessPath) -ine 'pwsh.exe' -or
      [Environment]::GetCommandLineArgs() -inotcontains '-NoProfile' -or
      [Environment]::GetCommandLineArgs() -inotcontains '-NonInteractive') { throw 'Fresh noninteractive PowerShell Core 7 host required' }
  if ($null -ne ('RetainedInputs' -as [type])) { throw 'RetainedInputs type already loaded' }
  $stream = [IO.File]::Open($interop,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
  $failure = $null
  try {
    $digest = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)).ToLowerInvariant()
    if ($digest -cne '76c97bc07ada9c4f8335a23b4eb12ded531a83b72c11d78eda9a85a39934cbaa') { throw 'Interop identity mismatch' }
    $stream.Position = 0
    $reader = [IO.StreamReader]::new($stream)
    Add-Type -TypeDefinition ($reader.ReadToEnd())
  } catch { $failure = $_ }
  finally { try { $stream.Dispose() } catch { Add-CleanupError 'interop source close' $_ } }
  if ($null -ne $failure) { throw $failure }
  if ($script:audit.secondary.Count) { throw 'Interop source close failed' }
}
function Write-Record($path, $record) {
  $bytes = [Text.UTF8Encoding]::new($false).GetBytes((ConvertTo-Json -InputObject $record -Depth 10) + "`n")
  $priorErrors = $script:audit.secondary.Count
  $file = [IO.File]::Open($path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
  $failure = $null
  try { $file.Write($bytes,0,$bytes.Length); $file.Flush($true) } catch { $failure = $_ }
  finally { try { $file.Dispose() } catch { Add-CleanupError 'record close' $_ } }
  if ($null -ne $failure) { throw $failure }
  if ($script:audit.secondary.Count -ne $priorErrors) { throw 'Record close failed' }
}
function Restore-ControllerEnvironment {
  if (!$script:audit.envSaved) { return }
  try { [Environment]::SetEnvironmentVariable('TEMP',$script:audit.temp,'Process') }
  catch { Add-CleanupError 'TEMP restore' $_ }
  try { [Environment]::SetEnvironmentVariable('TMP',$script:audit.tmp,'Process') }
  catch { Add-CleanupError 'TMP restore' $_ }
}
$script:audit = @{stage='preflight';started=$false;rootCreated=$false;envSaved=$false;
  temp=$null;tmp=$null;suppress=$false;secondary=[Collections.Generic.List[string]]::new()}
$pins = $null; $primary = $null; $postchecks = $null; $record = $null
try {
  Initialize-Scratch
  $script:audit.stage = 'interop'; Load-RetainedInterop
  $pins = [RetainedInputs]::new()
  $script:audit.stage = 'pinning'; Pin-Inputs $pins; $pins.PinParents("$root/placeholder"); Check-Sources
  foreach ($name in @('temp','roaming','local')) { New-Item -ItemType Directory -Path (Join-Path $root $name) | Out-Null }
  $script:audit.stage = 'compile'; $run = Invoke-FixedHelper 'compile' 0
  $receipt = @(Check-Receipt "$root/outer.receipt")
  $observedDriverExit = [int]($receipt[2].Substring('root_exit='.Length))
  $script:audit.stage = 'postcheck'; $postrun = Invoke-FixedHelper 'postcheck' $observedDriverExit
  $postreceipt = @(Check-Receipt "$root/postcheck.receipt")
  $script:audit.stage = 'validation'; $record = Read-DiscoveryResult $pins $run $postrun $receipt $postreceipt
  $record.host = [ordered]@{executable=[Environment]::ProcessPath;version=$PSVersionTable.PSVersion.ToString();profile='NoProfile'}
} catch {
  $primary = $_
  try { if (Test-StorageFailure $primary) { $script:audit.suppress = $true } }
  catch { $script:audit.suppress = $true }
  try { $postchecks = Get-FailurePostchecks $pins } catch { Add-CleanupError 'failure postchecks' $_ }
} finally {
  if ($null -ne $pins) { try { $pins.Dispose() } catch { Add-CleanupError 'retained handle close' $_ } }
  Restore-ControllerEnvironment
}
if ($null -ne $primary -or $script:audit.secondary.Count) {
  if ($null -eq $primary) { $reason = 'Cleanup failed after task work' } else { $reason = $primary.ToString() }
  $settlement = if ($script:audit.started) { 'helper started; settlement requires strict receipt' } else { 'helper not started; compiler not started' }
  $failure = [ordered]@{schema='cleanup-claim-discovery-failed-v1';source=$ReviewedSourceSHA;stage=$script:audit.stage;
    reason=$reason;secondary=@($script:audit.secondary);postchecks=$postchecks;settlement=$settlement}
  if ($script:audit.rootCreated -and !$script:audit.suppress) {
    try { Write-Record "$root/failure.json" $failure } catch { Add-CleanupError 'failure evidence write' $_ }
  }
  Write-Error ($failure | ConvertTo-Json -Depth 6 -Compress)
  exit 1
}
Write-Record "$root/result.json" $record
Write-Output "Discovery compile settled; helper=$($run.pid); source=$ReviewedSourceSHA; dependencies require review"
