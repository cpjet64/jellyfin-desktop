param([Parameter(Mandatory)][string]$ReviewedProposalSHA,
      [Parameter(Mandatory)][string]$ControllerSHA256,[Parameter(Mandatory)][string]$ProposalSHA256)
$ErrorActionPreference = 'Stop'
$repo = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-fixture-detached-20261114'
$policyRepo = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/reconcile-build-policy-20261113'
$pure = 'E:/CursorAI/JellyfinDesktop/deps/reconcile-20261113/meta-vctip-01'
$manifestPath = "$repo/deps/fixture-detached-v1/qualification-manifest.json"
$proposalDir = "$policyRepo/dev/windows/proof/proposals/vctip-identity-v1"
$proposalPath = "$proposalDir/proposal.json"
$client = "$proposalDir/client.py"
$helper = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/integration/deps/w1214-proof/job_launch_prod.exe'
$python = 'C:/Python314/python.exe'
$sha = 'd0bb016df169b663567e88fd5586ab7c008e9554'
$manifestHash = 'DBE55EB7C529D11386700A60DAB96F56AB12BC7B3C74E6B0E55277FE6CA4B7E0'
$clientHash = '6B42585ABA038B0FDFD301DC9E52545913D50A01023C0520B3CE19FCEF7F0235'
$helperHash = '7AE01DFD262A18BF863C057D570993B2C4DC529FCC03AC1E58D15F0DEDB0EBD5'
$pythonHash = '7CA24F26D6E3F463419EE4F537DDD3ACD312C38FE45E678CCE08572F26A8BD1A'
$internalHash = '80b842ae0d7ad4c3384b9eacfb9c492962febebae449c90a2b18d6b1e4f25915'
$externalHash = '42016ce3f5972c5cbcf19e7c1b0b86fcbea879dcf834e42add85670b8fa73e77'
if ($ControllerSHA256 -notmatch '^[0-9a-fA-F]{64}$' -or
    (Get-FileHash -LiteralPath $PSCommandPath).Hash -ine $ControllerSHA256) { throw 'Controller identity mismatch' }
if ($ReviewedProposalSHA -cnotmatch '^[0-9a-f]{40}$' -or $ProposalSHA256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'Invalid proposal identity' }
function Assert-FreshRoot {
  $expected = [IO.Path]::GetFullPath('E:/CursorAI/JellyfinDesktop/deps/reconcile-20261113/meta-vctip-01')
  if ([IO.Path]::GetFullPath($pure) -ine $expected -or (Test-Path -LiteralPath $pure)) { throw 'Evidence root not fresh' }
  $parent = [IO.DirectoryInfo]::new([IO.Path]::GetDirectoryName($expected))
  while ($null -ne $parent) {
    if (!$parent.Exists -or ($parent.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unsafe evidence parent' }
    $parent = $parent.Parent
  }
}
function Check-Inputs {
  if ((Get-FileHash -LiteralPath $manifestPath).Hash -cne $manifestHash) { throw 'Manifest identity mismatch' }
  $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
  if ($manifest.source_sha -cne $sha -or $manifest.internal_sha256 -cne $internalHash -or
      $manifest.external_sha256 -cne $externalHash -or @($manifest.files).Count -ne 21) { throw 'Manifest contents mismatch' }
  $head = & git.exe -C $repo rev-parse HEAD
  if ($LASTEXITCODE -ne 0 -or $head -cne $sha) { throw 'Source HEAD mismatch' }
  $status = @(& git.exe -C $repo status --porcelain --untracked-files=all --ignore-submodules=none)
  if ($LASTEXITCODE -ne 0 -or $status.Count) { throw 'Source not clean or status unavailable' }
  foreach ($entry in $manifest.files) {
    $file = Join-Path $repo $entry.path
    if ((Get-Item -LiteralPath $file).Length -ne $entry.bytes -or
        (Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant() -cne $entry.sha256) { throw "Source bytes changed: $($entry.path)" }
    $blob = & git.exe -C $repo -c core.autocrlf=false hash-object --no-filters -- $file
    if ($LASTEXITCODE -ne 0 -or $blob -cne $entry.git_blob) { throw "Source blob changed: $($entry.path)" }
    $tracked = & git.exe -C $repo rev-parse "${sha}:$($entry.path)"
    if ($LASTEXITCODE -ne 0 -or $tracked -cne $blob) { throw "Tracked blob mismatch: $($entry.path)" }
  }
  $policyHead = & git.exe -C $policyRepo rev-parse HEAD
  if ($LASTEXITCODE -ne 0 -or $policyHead -cne $ReviewedProposalSHA) { throw 'Proposal revision mismatch' }
  $policyStatus = @(& git.exe -C $policyRepo status --porcelain --untracked-files=all --ignore-submodules=none)
  if ($LASTEXITCODE -ne 0 -or $policyStatus.Count) { throw 'Proposal status unavailable or dirty' }
  if ((Get-FileHash -LiteralPath $client).Hash -cne $clientHash -or
      (Get-FileHash -LiteralPath $proposalPath).Hash -ine $ProposalSHA256) { throw 'Client or proposal identity mismatch' }
  foreach ($entry in @(@('client.py',$client),@('controller.ps1',$PSCommandPath),@('proposal.json',$proposalPath))) {
    $relative = 'dev/windows/proof/proposals/vctip-identity-v1/' + $entry[0]
    $blob = & git.exe -C $policyRepo -c core.autocrlf=false hash-object --no-filters -- $entry[1]
    if ($LASTEXITCODE -ne 0) { throw 'Proposal raw hash unavailable' }
    $tracked = & git.exe -C $policyRepo rev-parse "${ReviewedProposalSHA}:$relative"
    if ($LASTEXITCODE -ne 0 -or $tracked -cne $blob) { throw 'Proposal tracked blob mismatch' }
  }
  if ((Get-FileHash -LiteralPath $python).Hash -cne $pythonHash -or (Get-Item -LiteralPath $python).Length -ne 106328 -or
      (Get-FileHash -LiteralPath $helper).Hash -cne $helperHash) { throw 'Runtime or helper mismatch' }
  return $head
}
function Check-Receipt($path) {
  $item = Get-Item -LiteralPath $path -Force
  if ($item.Length -gt 1024 -or $item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unsafe receipt' }
  $lines = @(Get-Content -LiteralPath $path)
  if ($lines.Count -ne 9 -or $lines[0] -cne 'receipt_version=1' -or $lines[1] -notmatch '^root_pid=[1-9][0-9]*$' -or
      $lines[2] -cne 'root_exit=0' -or $lines[3] -cne 'timeout=0 active=0' -or
      $lines[4] -notmatch '^handles_before=[0-9]+ handles_after=[0-9]+ measured=[01]$' -or
      $lines[5] -cne 'owned_opened=6 owned_closed=6 close_failed=0' -or $lines[6] -cne 'handles_complete=1' -or
      $lines[7] -cne 'final_result=0' -or $lines[8] -cne 'proof_complete=1') { throw 'Invalid outer receipt' }
  return $lines
}
Assert-FreshRoot
$startHead = Check-Inputs
if ((Get-PSDrive E).Free -lt 20GB) { throw 'Insufficient storage; stop' }
New-Item -ItemType Directory -Path $pure | Out-Null
foreach ($name in @('temp','local','roaming','profile')) {
  $path = Join-Path $pure $name
  if (Test-Path -LiteralPath $path) { throw "Profile already exists: $name" }
  New-Item -ItemType Directory -Path $path | Out-Null
}
$argsList = @("$pure/outer.receipt",'180000',"$repo/dev/windows/proof",$python,'-B',$client)
$psi = [Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $helper; $psi.WorkingDirectory = "$repo/dev/windows/proof"
$psi.UseShellExecute = $false; $psi.CreateNoWindow = $true; $psi.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
$psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
foreach ($value in $argsList) { $psi.ArgumentList.Add($value) }
$psi.Environment['TEMP'] = "$pure/temp"; $psi.Environment['TMP'] = "$pure/temp"; $psi.Environment['TMPDIR'] = "$pure/temp"
$psi.Environment['LOCALAPPDATA'] = "$pure/local"; $psi.Environment['APPDATA'] = "$pure/roaming"
$psi.Environment['USERPROFILE'] = "$pure/profile"; $psi.Environment['PYTHONDONTWRITEBYTECODE'] = '1'
$null = $psi.Environment.Remove('PYTHONPATH'); $null = $psi.Environment.Remove('PYTHONHOME')
foreach ($name in @('BUILD_ONLY_NATIVE_QUALIFY','BUILD_ONLY_NATIVE_DIAGNOSTIC','BUILD_ONLY_NATIVE_ROOT','BUILD_ONLY_NATIVE_HELPER')) {
  $null = $psi.Environment.Remove($name)
  if ($psi.Environment.ContainsKey($name)) { throw 'Native qualification environment remains present' }
}
$stdout = [IO.File]::Open("$pure/stdout.log",[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try {
  $stderr = [IO.File]::Open("$pure/stderr.log",[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
  try {
    $proc = [Diagnostics.Process]::new(); $proc.StartInfo = $psi
    $watch = [Diagnostics.Stopwatch]::StartNew(); $started = [DateTime]::UtcNow
    try {
      if (!$proc.Start()) { throw 'Helper did not start' }
      $pidValue = $proc.Id; $null = $proc.Handle
      $outTask = $proc.StandardOutput.BaseStream.CopyToAsync($stdout)
      $errTask = $proc.StandardError.BaseStream.CopyToAsync($stderr)
      $finished = $false
      while ($watch.ElapsedMilliseconds -lt 200000) {
        if ($outTask.IsFaulted -or $errTask.IsFaulted) { throw 'Log drain failed; stop immediately' }
        $slice = [Math]::Min(200, 200000 - [int]$watch.ElapsedMilliseconds)
        if ($proc.WaitForExit($slice)) { $finished = $true; break }
      }
      if (!$finished) {
        $proc.Kill()
        $remaining = [Math]::Max(0, 210000 - [int]$watch.ElapsedMilliseconds)
        if (!$proc.WaitForExit($remaining)) { throw 'Helper unsettled; job settlement unknown' }
        throw 'Helper timed out; job settlement unknown'
      }
      $remaining = [Math]::Max(0, 210000 - [int]$watch.ElapsedMilliseconds)
      if (![Threading.Tasks.Task]::WaitAll(@($outTask,$errTask),$remaining)) { throw 'Log drains unsettled' }
      $exit = $proc.ExitCode; $ended = [DateTime]::UtcNow
      $stdout.Flush($true); $stderr.Flush($true)
    } finally {
      if ($null -ne $proc) {
        try {
          if (!$proc.HasExited) {
            $proc.Kill()
            $remaining = [Math]::Max(0, 210000 - [int]$watch.ElapsedMilliseconds)
            if (!$proc.WaitForExit($remaining)) { throw 'Helper unsettled during cleanup' }
            throw 'Helper interrupted; job settlement unknown'
          }
        } finally { $proc.Dispose() }
      }
    }
  } finally { $stderr.Dispose() }
} finally { $stdout.Dispose() }
if ($exit -ne 0) { throw "Helper returned $exit" }
$receipt = @(Check-Receipt "$pure/outer.receipt")
$log = [IO.File]::ReadAllText("$pure/stderr.log")
$outputLog = [IO.File]::ReadAllText("$pure/stdout.log")
if (($log + $outputLog) -match '(?i)no space left|disk full|not enough (space|storage)|quota exceeded|errno 28|winerror 112') {
  throw 'Storage failure reported; stop dependent writes'
}
if ($log.Length -ne 0) { throw 'Identity client emitted unexpected stderr' }
if ($outputLog.Length -gt 4096) { throw 'Identity result too large' }
$identity = $outputLog | ConvertFrom-Json -AsHashtable
$keys = @('path','final_path','sha256','size','volume_serial','file_index_high','file_index_low',
          'version_metadata','version_basis','owned')
if ($identity -isnot [Collections.IDictionary] -or
    (Compare-Object $keys @($identity.Keys)) -or
    $identity.owned -isnot [Collections.IDictionary] -or
    (Compare-Object @('opened','closed','failed') @($identity.owned.Keys))) { throw 'Invalid identity result shape' }
if ($identity.path -cne 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\vctip.exe' -or
    $identity.final_path -ine ('\\?\' + $identity.path) -or
    $identity.sha256 -cne 'd591358c6aee4b673562ca3b3a15c250e612708394fe3b44a7a7eda2470570a5' -or
    $identity.size -ne 335288 -or $identity.version_metadata -cne '14.44.35229.0' -or
    $identity.version_basis -cne 'previously observed metadata bound to pinned bytes; not re-observed' -or
    $identity.owned.opened -le 0 -or $identity.owned.opened -ne $identity.owned.closed -or $identity.owned.failed -ne 0) {
  throw 'Identity result mismatch'
}
foreach ($value in @($identity.volume_serial,$identity.file_index_high,$identity.file_index_low)) {
  if ($value -isnot [int] -and $value -isnot [long] -or $value -lt 0 -or $value -gt [uint32]::MaxValue) { throw 'Invalid file identity component' }
}
if ($identity.file_index_high -eq 0 -and $identity.file_index_low -eq 0) { throw 'Invalid empty file identity' }
$endHead = Check-Inputs
$result = [ordered]@{source_before=$startHead;source_after=$endHead;helper_pid=$pidValue;helper_exit=$exit;
  started_utc=$started.ToString('o');ended_utc=$ended.ToString('o');elapsed_ms=$watch.ElapsedMilliseconds;
  executable=$helper;arguments=$argsList;controller_sha256=(Get-FileHash -LiteralPath $PSCommandPath).Hash.ToLowerInvariant();
  stdout_sha256=(Get-FileHash -LiteralPath "$pure/stdout.log").Hash.ToLowerInvariant();
  stderr_sha256=(Get-FileHash -LiteralPath "$pure/stderr.log").Hash.ToLowerInvariant();
  receipt_sha256=(Get-FileHash -LiteralPath "$pure/outer.receipt").Hash.ToLowerInvariant();receipt=$receipt;
  identity=$identity;client_sha256=$clientHash.ToLowerInvariant();proposal_sha256=$ProposalSHA256.ToLowerInvariant();
  proposal_source=$ReviewedProposalSHA;manifest_sha256=$manifestHash.ToLowerInvariant()}
$data = [Text.UTF8Encoding]::new($false).GetBytes((ConvertTo-Json -InputObject $result -Depth 6) + "`n")
$file = [IO.File]::Open("$pure/result.json",[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try { $file.Write($data,0,$data.Length); $file.Flush($true) } finally { $file.Dispose() }
Write-Output "PASS read-only identity helper_exit=$exit elapsed_ms=$($watch.ElapsedMilliseconds) source=$endHead"
