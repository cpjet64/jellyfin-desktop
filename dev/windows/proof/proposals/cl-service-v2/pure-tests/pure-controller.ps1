param([Parameter(Mandatory)][string]$ReviewedProposalSHA,
      [Parameter(Mandatory)][string]$ReviewedControllerSHA256)
$ErrorActionPreference = 'Stop'
$v1 = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-fixture-detached-20261114'
$v2 = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/compiler-contract-v2-review-20261114'
$proposal = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/compiler-pure-tests-v2-20261114'
$root = "$v2/deps/pure-test-v2-20261114-02"
$proof = "$v2/dev/windows/proof"
$driver = "$proposal/dev/windows/proof/proposals/cl-service-v2/pure-tests/pure-driver.py"
$manifest = "$v1/deps/fixture-detached-v1/qualification-manifest.json"
$interop = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-build-policy-20261113/dev/windows/proof/proposals/cl-cleanup-claim-v1/RetainedInputs.cs'
$python = 'C:/Python314/python.exe'
$helper = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/integration/deps/w1214-proof/job_launch_prod.exe'
$files = @(
  @('build_only_v2/profile.py','e905da84c963c6669ceb184027fe72da54e3a48743b8aa85a7f5da57e34887a7',7083),
  @('build_only_v2/lifecycle.py','04eea626daec5df142caf4ea03acfd4a408aa4947216609be270fab160594140',10180),
  @('build_only_v2/supervisor.py','1e9ba410ad76691e49bc94efba8a4e467d774b6eeed4bb32b60f7ace0d902fbb',11493),
  @('build_only_v2/receipt.py','73ad320f401fd30b868500962f3d82660bc5b1ccab6ac0b9547cfb73761fdc0b',7146),
  @('build_only_v2/postcheck.py','38af20981d2875de16361e8d0978565340fc7ab03070ebc4b661e4088627ac2e',7249),
  @('test_build_only_v2_profile.py','c18258a58dc43fc5339b70b71e20cedd732406dacdb8fec8c1172bf57984105e',4223),
  @('test_build_only_v2_receipt.py','b7e13f1827e4f800e4db0903a34eb75a8dfb4b822cc66dcf3b4e279bedc843c5',10311),
  @('tests/test_v2_lifecycle_contract.py','3c42a2594b233a3ff9aef743881a3d7f371a08ff0fe7adc5a280c5705904f2cc',10212))

function Check-Tree($path,$sha) {
  $actual = & git.exe -C $path rev-parse HEAD
  if ($LASTEXITCODE -ne 0 -or $actual -cne $sha) { throw 'HEAD mismatch' }
  $status = @(& git.exe -C $path status --porcelain --untracked-files=all --ignore-submodules=none)
  if ($LASTEXITCODE -ne 0 -or $status.Count) { throw 'Worktree not clean' }
}
function Check-Blob($repo,$sha,$relative) {
  $raw = & git.exe -C $repo -c core.autocrlf=false hash-object --no-filters -- (Join-Path $repo $relative)
  if ($LASTEXITCODE -ne 0) { throw 'Raw source unavailable' }
  $tracked = & git.exe -C $repo rev-parse "${sha}:$relative"
  if ($LASTEXITCODE -ne 0 -or $raw -cne $tracked) { throw 'Source blob mismatch' }
}
function Verify-Sources {
  Check-Tree $v1 'd0bb016df169b663567e88fd5586ab7c008e9554'
  Check-Tree $v2 '05a32b50c1a393d029b0d16485f47976b200020c'
  Check-Tree $proposal $ReviewedProposalSHA
  foreach ($entry in $files) { Check-Blob $v2 '05a32b50c1a393d029b0d16485f47976b200020c' ('dev/windows/proof/' + $entry[0]) }
  foreach ($path in @('pure-controller.ps1','pure-driver.py')) {
    Check-Blob $proposal $ReviewedProposalSHA ('dev/windows/proof/proposals/cl-service-v2/pure-tests/' + $path)
  }
}
function Pin-Inputs($pins) {
  $none=[System.Management.Automation.Language.NullString]::Value
  $pins.PinFile($manifest,'dbe55eb7c529d11386700a60dab96f56ab12bc7b3c74e6b0e55277fe6ca4b7e0',(Get-Item -LiteralPath $manifest).Length,$none)
  $m = Get-Content -Raw -LiteralPath $manifest | ConvertFrom-Json
  if ($m.source_sha -cne 'd0bb016df169b663567e88fd5586ab7c008e9554' -or @($m.files).Count -ne 21) { throw 'Manifest mismatch' }
  foreach ($entry in $m.files) {
    Check-Blob $v1 'd0bb016df169b663567e88fd5586ab7c008e9554' $entry.path
    $pins.PinFile((Join-Path $v1 $entry.path),$entry.sha256,$entry.bytes,$none)
  }
  foreach ($entry in $files) { $pins.PinFile((Join-Path $proof $entry[0]),$entry[1],$entry[2],$none) }
  $pins.PinFile($driver,'8439f9ac4ddf5ed8d02edf5a8bd050e6837eff83e20d13b4b8af48295b0f330f',(Get-Item -LiteralPath $driver).Length,$none)
  $pins.PinFile($PSCommandPath,$ReviewedControllerSHA256,(Get-Item -LiteralPath $PSCommandPath).Length,$none)
  $pins.PinFile($interop,'fc754851846ebcea79b0f719c02602b8ad86e505272586b5cfd50148d3117a89',(Get-Item -LiteralPath $interop).Length,$none)
  $pins.PinFile($python,'7ca24f26d6e3f463419ee4f537ddd3acd312c38fe45e678cce08572f26a8bd1a',106328,$none)
  $pins.PinFile($helper,'7ae01dfd262a18bf863c057d570993b2c4dc529fcc03ac1e58d15f0dedb0ebd5',166400,$none)
  $pins.PinParents("$root/placeholder")
}
function Assert-Receipt {
  $item=Get-Item -LiteralPath "$root/outer.receipt" -Force
  if ($item.Length -gt 1024 -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unsafe outer receipt' }
  $lines = @(Get-Content -LiteralPath "$root/outer.receipt")
  $patterns = @('^receipt_version=1$','^root_pid=[1-9][0-9]*$','^root_exit=0$','^timeout=0 active=0$',
    '^handles_before=[0-9]+ handles_after=[0-9]+ measured=1$','^owned_opened=6 owned_closed=6 close_failed=0$',
    '^handles_complete=1$','^final_result=0$','^proof_complete=1$')
  if ($lines.Count -ne 9) { throw 'Outer receipt field count' }
  for ($i=0; $i -lt 9; $i++) { if ($lines[$i] -cnotmatch $patterns[$i]) { throw 'Outer receipt nonpass' } }
}
function Preflight-Inputs {
  if ((Get-FileHash -LiteralPath $manifest).Hash.ToLowerInvariant() -cne 'dbe55eb7c529d11386700a60dab96f56ab12bc7b3c74e6b0e55277fe6ca4b7e0') { throw 'Manifest hash mismatch' }
  $m=Get-Content -Raw -LiteralPath $manifest | ConvertFrom-Json
  if ($m.source_sha -cne 'd0bb016df169b663567e88fd5586ab7c008e9554' -or @($m.files).Count -ne 21) { throw 'Manifest mismatch' }
  foreach ($entry in $m.files) {
    $path=Join-Path $v1 $entry.path; Check-Blob $v1 'd0bb016df169b663567e88fd5586ab7c008e9554' $entry.path
    if ((Get-Item -LiteralPath $path).Length -ne $entry.bytes -or (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -cne $entry.sha256) { throw 'V1 input mismatch' }
  }
  foreach ($entry in $files) {
    $path=Join-Path $proof $entry[0]
    if ((Get-Item -LiteralPath $path).Length -ne $entry[2] -or (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -cne $entry[1]) { throw 'V2 input mismatch' }
  }
  foreach ($pair in @(@($driver,'8439f9ac4ddf5ed8d02edf5a8bd050e6837eff83e20d13b4b8af48295b0f330f'),
      @($python,'7ca24f26d6e3f463419ee4f537ddd3acd312c38fe45e678cce08572f26a8bd1a'),
      @($helper,'7ae01dfd262a18bf863c057d570993b2c4dc529fcc03ac1e58d15f0dedb0ebd5'))) {
    if ((Get-FileHash -LiteralPath $pair[0]).Hash.ToLowerInvariant() -cne $pair[1]) { throw 'Runner input mismatch' }
  }
}
function Assert-Private($path) {
  $user=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value
  $allowed=@($user,'S-1-5-18','S-1-5-32-544'); $acl=Get-Acl -LiteralPath $path
  if ($acl.GetOwner([Security.Principal.SecurityIdentifier]).Value -cne $user) { throw 'Output owner mismatch' }
  if (!$acl.AreAccessRulesProtected) { throw 'Output ACL inheritance not protected' }
  $rules=@($acl.GetAccessRules($true,$true,[Security.Principal.SecurityIdentifier]))
  if ($rules.Count -ne 3) { throw 'Unexpected output ACL rule count' }
  $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
  foreach ($rule in $rules) {
    if ($rule.IdentityReference.Value -cnotin $allowed -or !$seen.Add($rule.IdentityReference.Value) -or
        $rule.AccessControlType -ne 'Allow' -or $rule.IsInherited -or
        ($rule.FileSystemRights -band [Security.AccessControl.FileSystemRights]::FullControl) -ne [Security.AccessControl.FileSystemRights]::FullControl -or
        $rule.InheritanceFlags -ne ([Security.AccessControl.InheritanceFlags]::ContainerInherit -bor [Security.AccessControl.InheritanceFlags]::ObjectInherit) -or
        $rule.PropagationFlags -ne [Security.AccessControl.PropagationFlags]::None) { throw 'Unexpected output ACL rule' }
  }
}
function Set-NewPrivate($path) {
  $allowed=@($script:createdParent,$script:createdRoot,"$root/host-temp","$root/pycache")
  if ($path -cnotin $allowed -or !$script:createdPaths.Contains($path)) { throw 'Directory ownership not established' }
  $item=Get-Item -LiteralPath $path -Force -ErrorAction Stop
  if (!$item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
      [IO.Path]::GetFullPath($item.FullName) -ine [IO.Path]::GetFullPath($path)) { throw 'Unsafe task directory' }
  $user=[Security.Principal.WindowsIdentity]::GetCurrent().User
  $acl=[Security.AccessControl.DirectorySecurity]::new()
  $acl.SetOwner($user); $acl.SetAccessRuleProtection($true,$false)
  $inherit=[Security.AccessControl.InheritanceFlags]::ContainerInherit -bor [Security.AccessControl.InheritanceFlags]::ObjectInherit
  foreach ($sid in @($user.Value,'S-1-5-18','S-1-5-32-544')) {
    $identity=[Security.Principal.SecurityIdentifier]::new($sid)
    $rule=[Security.AccessControl.FileSystemAccessRule]::new($identity,[Security.AccessControl.FileSystemRights]::FullControl,
      $inherit,[Security.AccessControl.PropagationFlags]::None,[Security.AccessControl.AccessControlType]::Allow)
    $acl.AddAccessRule($rule)
  }
  Set-Acl -LiteralPath $path -AclObject $acl -ErrorAction Stop
  Assert-Private $path
}
function Invoke-Pure {
  $psi = [Diagnostics.ProcessStartInfo]::new()
  $psi.FileName=$helper; $psi.WorkingDirectory="$v1/dev/windows/proof"
  $psi.UseShellExecute=$false; $psi.CreateNoWindow=$true; $psi.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
  $psi.RedirectStandardOutput=$true; $psi.RedirectStandardError=$true
  foreach ($arg in @("$root/outer.receipt",'60000',"$v1/dev/windows/proof",$python,'-I','-S','-B','-X',"pycache_prefix=$root/pycache",$driver)) { $psi.ArgumentList.Add($arg) }
  $null=$psi.Environment.Remove('PYTHONPATH'); $null=$psi.Environment.Remove('PYTHONHOME')
  $out=$null; $err=$null; $proc=$null; $started=$false; $failure=$null
  $secondary=[Collections.Generic.List[string]]::new(); $watch=[Diagnostics.Stopwatch]::StartNew()
  try {
    $out=[IO.File]::Open("$root/stdout.log",'CreateNew','Write','None')
    $err=[IO.File]::Open("$root/stderr.log",'CreateNew','Write','None')
    $proc=[Diagnostics.Process]::new(); $proc.StartInfo=$psi
    if (!$proc.Start()) { throw 'Helper launch failed' }; $started=$true
    $outTask=$proc.StandardOutput.BaseStream.CopyToAsync($out); $errTask=$proc.StandardError.BaseStream.CopyToAsync($err)
    if (!$proc.WaitForExit(65000)) { throw 'Helper timeout; settlement unknown' }
    if (![Threading.Tasks.Task]::WaitAll(@($outTask,$errTask),[Math]::Max(0,70000-[int]$watch.ElapsedMilliseconds))) { throw 'Log drain timeout' }
    if ($proc.ExitCode -ne 0) { throw 'Helper exit nonzero' }
    $out.Flush($true); $err.Flush($true)
  } catch { $failure=$_ } finally {
    if ($null -ne $proc) {
      try {
        if ($started -and !$proc.HasExited) { $proc.Kill(); if (!$proc.WaitForExit([Math]::Max(0,70000-[int]$watch.ElapsedMilliseconds))) { throw 'Helper unresolved' } }
      } catch { $secondary.Add("helper settlement: $_") }
      try { $proc.Dispose() } catch { $secondary.Add("helper close: $_") }
    }
    if ($null -ne $err) { try { $err.Dispose() } catch { $secondary.Add("stderr close: $_") } }
    if ($null -ne $out) { try { $out.Dispose() } catch { $secondary.Add("stdout close: $_") } }
  }
  if ($null -ne $failure -or $secondary.Count) { throw "Helper nonpass: $failure; cleanup=$($secondary -join '; ')" }
}

function Initialize-Run {
  if ($PSVersionTable.PSEdition -cne 'Core' -or $PSVersionTable.PSVersion.Major -ne 7 -or
      [Environment]::GetCommandLineArgs() -inotcontains '-NoProfile' -or
      [Environment]::GetCommandLineArgs() -inotcontains '-NonInteractive') { throw 'Fresh noninteractive PowerShell 7 required' }
  if ($ReviewedProposalSHA -cnotmatch '^[0-9a-f]{40}$' -or $ReviewedControllerSHA256 -cnotmatch '^[0-9a-f]{64}$' -or
      (Get-FileHash -LiteralPath $PSCommandPath).Hash.ToLowerInvariant() -cne $ReviewedControllerSHA256) { throw 'Review identity mismatch' }
  Verify-Sources
  Preflight-Inputs
  if ((Get-PSDrive E).Free -lt 20GB) { throw 'Insufficient storage' }
  if (Test-Path -LiteralPath $root) { throw 'Output root already exists' }
  $expectedParent='E:/CursorAI/JellyfinDesktop/deps/worktrees/compiler-contract-v2-review-20261114/deps'
  if ([IO.Path]::GetFullPath([IO.Path]::GetDirectoryName($root)) -ine [IO.Path]::GetFullPath($expectedParent)) { throw 'Unexpected output parent' }
  $ancestor=[IO.DirectoryInfo]::new([IO.Path]::GetFullPath($v2))
  while ($null -ne $ancestor) {
    if (!$ancestor.Exists -or ($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unsafe output ancestry' }
    $ancestor=$ancestor.Parent
  }
  if ((Get-FileHash -LiteralPath $interop).Hash.ToLowerInvariant() -cne 'fc754851846ebcea79b0f719c02602b8ad86e505272586b5cfd50148d3117a89') { throw 'Interop mismatch' }
  if (Test-Path -LiteralPath $expectedParent) { throw 'Existing output parent ownership unestablished' }
  New-Item -ItemType Directory -Path $expectedParent -ErrorAction Stop | Out-Null
  $script:createdParent=$expectedParent
  $null=$script:createdPaths.Add($expectedParent)
  $parent=Get-Item -LiteralPath $expectedParent -Force -ErrorAction Stop
  if (!$parent.PSIsContainer -or ($parent.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unsafe created output parent' }
  $ancestor=[IO.DirectoryInfo]::new([IO.Path]::GetFullPath($expectedParent))
  while ($null -ne $ancestor) {
    if (!$ancestor.Exists -or ($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Output ancestry changed' }
    $ancestor=$ancestor.Parent
  }
  Set-NewPrivate $expectedParent
  New-Item -ItemType Directory -Path $root -ErrorAction Stop | Out-Null
  $script:createdRoot=$root
  $null=$script:createdPaths.Add($root); Set-NewPrivate $root
  New-Item -ItemType Directory -Path "$root/host-temp" -ErrorAction Stop | Out-Null
  $null=$script:createdPaths.Add("$root/host-temp"); Set-NewPrivate "$root/host-temp"
  New-Item -ItemType Directory -Path "$root/pycache" -ErrorAction Stop | Out-Null
  $null=$script:createdPaths.Add("$root/pycache"); Set-NewPrivate "$root/pycache"
  foreach ($path in @($expectedParent,$root,"$root/host-temp","$root/pycache")) { Assert-Private $path }
}

$oldTemp=[Environment]::GetEnvironmentVariable('TEMP','Process')
$oldTmp=[Environment]::GetEnvironmentVariable('TMP','Process')
$pins=$null; $primary=$null; $cleanup=[Collections.Generic.List[string]]::new()
$script:createdParent=$null; $script:createdRoot=$null
$script:createdPaths=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
try {
  Initialize-Run
  [Environment]::SetEnvironmentVariable('TEMP',"$root/host-temp",'Process')
  [Environment]::SetEnvironmentVariable('TMP',"$root/host-temp",'Process')
  if ($null -ne ('RetainedInputs' -as [type])) { throw 'Interop type already loaded' }
  $stream=[IO.File]::Open($interop,'Open','Read','Read')
  $interopFailure=$null; $interopCloseFailure=$null
  try {
    $digest=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream)).ToLowerInvariant()
    if ($digest -cne 'fc754851846ebcea79b0f719c02602b8ad86e505272586b5cfd50148d3117a89') { throw 'Interop changed' }
    $stream.Position=0; $reader=[IO.StreamReader]::new($stream)
    Add-Type -TypeDefinition ($reader.ReadToEnd())
  } catch { $interopFailure=$_ }
  finally { try { $stream.Dispose() } catch { $interopCloseFailure=$_ } }
  if ($null -ne $interopFailure -or $null -ne $interopCloseFailure) {
    throw "Interop failed: primary=$interopFailure; close=$interopCloseFailure"
  }
  $pins=[RetainedInputs]::new(); Pin-Inputs $pins; Verify-Sources; $pins.VerifyAll()
  if (@(Get-ChildItem -LiteralPath "$root/pycache" -Force).Count) { throw 'Cache not empty' }
  Invoke-Pure
  $null=$pins.ObserveFile("$root/outer.receipt"); $null=$pins.ObserveFile("$root/stdout.log"); $null=$pins.ObserveFile("$root/stderr.log")
  Assert-Receipt
  $markers=@(Get-Content -LiteralPath "$root/stdout.log" | Where-Object { $_ -cmatch '^PURE_RESULT tests=[1-9][0-9]* failures=0 errors=0 skipped=0$' })
  if ($markers.Count -ne 1) { throw 'Pure test count or outcome missing' }
  $pins.VerifyAll(); Verify-Sources
} catch { $primary=$_ }
finally {
  if ($null -ne $pins) { try { $pins.Dispose() } catch { $cleanup.Add("pin close: $_") } }
  try {
    [Environment]::SetEnvironmentVariable('TEMP',$oldTemp,'Process')
    if ([Environment]::GetEnvironmentVariable('TEMP','Process') -cne $oldTemp) { throw 'TEMP restore mismatch' }
  } catch { $cleanup.Add("TEMP restore: $_") }
  try {
    [Environment]::SetEnvironmentVariable('TMP',$oldTmp,'Process')
    if ([Environment]::GetEnvironmentVariable('TMP','Process') -cne $oldTmp) { throw 'TMP restore mismatch' }
  } catch { $cleanup.Add("TMP restore: $_") }
}
if ($null -ne $primary -or $cleanup.Count) { throw "Pure lane nonpass; parent=$script:createdParent root=$script:createdRoot primary=$primary cleanup=$($cleanup -join '; ')" }
Write-Output 'Pure source suites passed; endpoint and native qualification remain unproved.'
