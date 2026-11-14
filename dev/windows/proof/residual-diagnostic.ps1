param(
  [Parameter(Mandatory)] [string] $ReviewedSourceSHA,
  [Parameter(Mandatory)] [string] $PackageManifestPath,
  [Parameter(Mandatory)] [string] $PackageManifestSHA256,
  [Parameter(Mandatory)] [string] $FreshEvidenceRoot
)
$ErrorActionPreference = 'Stop'
$repo = (& git.exe -C $PSScriptRoot rev-parse --show-toplevel)
if ($LASTEXITCODE -ne 0) { throw 'Repository root unavailable' }
$repo = [IO.Path]::GetFullPath($repo)
$base = 'ff831ccc7832d147f77dc4e69006ccda0190fa89'
$helper = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/integration/deps/w1214-proof/job_launch_prod.exe'
$python = 'C:/Python314/python.exe'
$helperHash = '7AE01DFD262A18BF863C057D570993B2C4DC529FCC03AC1E58D15F0DEDB0EBD5'
$pythonHash = '7CA24F26D6E3F463419EE4F537DDD3ACD312C38FE45E678CCE08572F26A8BD1A'
if ($ReviewedSourceSHA -cnotmatch '^[0-9a-f]{40}$' -or $PackageManifestSHA256 -cnotmatch '^[0-9A-Fa-f]{64}$') {
  throw 'Invalid reviewed source or manifest identity'
}
if (![IO.Path]::IsPathFullyQualified($PackageManifestPath) -or ![IO.Path]::IsPathFullyQualified($FreshEvidenceRoot)) {
  throw 'Absolute manifest and evidence paths required'
}
$evidence = [IO.Path]::GetFullPath($FreshEvidenceRoot)
$deps = [IO.Path]::GetFullPath((Join-Path $repo 'deps')).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
if (!$evidence.StartsWith($deps,[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $evidence)) {
  throw 'Fresh project-local evidence root required'
}
function Assert-NoReparseParent($Path) {
  $parent = [IO.DirectoryInfo]::new([IO.Path]::GetDirectoryName($Path))
  while ($null -ne $parent) {
    if (!$parent.Exists -or ($parent.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unsafe evidence parent' }
    $parent = $parent.Parent
  }
}
function Get-Digest($Value) {
  $json = ConvertTo-Json -InputObject $Value -Compress -Depth 10
  $hasher = [Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($json)))).Replace('-','').ToLowerInvariant() }
  finally { $hasher.Dispose() }
}
function Assert-Inputs {
  if ((Get-FileHash -LiteralPath $PackageManifestPath).Hash -ine $PackageManifestSHA256) { throw 'Package manifest hash mismatch' }
  $manifest = Get-Content -LiteralPath $PackageManifestPath -Raw | ConvertFrom-Json
  if ($manifest.source_sha -cne $ReviewedSourceSHA -or @($manifest.files).Count -ne 20) { throw 'Unexpected package manifest' }
  $head = & git.exe -C $repo rev-parse HEAD
  if ($LASTEXITCODE -ne 0 -or $head -cne $ReviewedSourceSHA) { throw 'Source revision mismatch' }
  $status = @(& git.exe -C $repo status --porcelain --untracked-files=all --ignore-submodules=none)
  if ($LASTEXITCODE -ne 0 -or $status.Count) { throw 'Source status dirty or unavailable' }
  $paths = @(& git.exe -C $repo diff --name-only --diff-filter=A $base $ReviewedSourceSHA)
  if ($LASTEXITCODE -ne 0) { throw 'Package added-file list unavailable' }
  $changed = @(& git.exe -C $repo diff --name-only $base $ReviewedSourceSHA)
  if ($LASTEXITCODE -ne 0 -or $paths.Count -ne 20 -or $changed.Count -ne 20) { throw 'Unexpected package file set' }
  $records = [Collections.Generic.List[object]]::new()
  foreach ($path in $paths) {
    $entries = @($manifest.files | Where-Object { $_.path -ceq $path })
    if ($entries.Count -ne 1) { throw 'Missing or duplicate package entry' }
    $entry = $entries[0]; $file = Join-Path $repo $path
    $hash = (Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant()
    $blob = & git.exe -C $repo -c core.autocrlf=false hash-object --no-filters -- $file
    if ($LASTEXITCODE -ne 0 -or $blob -cne $entry.git_blob -or $hash -cne $entry.sha256 -or
        (Get-Item -LiteralPath $file).Length -ne $entry.bytes) { throw "Package byte mismatch: $path" }
    $tracked = & git.exe -C $repo rev-parse "${ReviewedSourceSHA}:$path"
    if ($LASTEXITCODE -ne 0 -or $tracked -cne $blob) { throw "Tracked blob mismatch: $path" }
    $records.Add([object]@($path,$hash))
  }
  if ((Get-Digest $records.ToArray()) -cne $manifest.external_sha256) { throw 'External package digest mismatch' }
  $internal = [Collections.Generic.List[object]]::new()
  foreach ($name in @('__init__.py','identity.py','lifecycle.py','policy.py','receipt.py','receipt_file.py','spawn.py','supervisor.py','winapi.py')) {
    $item = @($manifest.files | Where-Object { $_.path -ceq "dev/windows/proof/build_only/$name" })
    if ($item.Count -ne 1) { throw 'Missing internal source' }
    $internal.Add([object]@($name,$item[0].sha256))
  }
  if ((Get-Digest $internal.ToArray()) -cne $manifest.internal_sha256) { throw 'Internal package digest mismatch' }
  if ((Get-FileHash -LiteralPath $python).Hash -cne $pythonHash -or (Get-Item -LiteralPath $python).Length -ne 106328 -or
      (Get-FileHash -LiteralPath $helper).Hash -cne $helperHash) { throw 'Python or helper identity mismatch' }
  return $head
}
function Assert-Receipt($Path, [int]$Exit) {
  $item = Get-Item -LiteralPath $Path -Force
  if ($item.Length -gt 1024 -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unsafe outer receipt' }
  $lines = @(Get-Content -LiteralPath $Path)
  if ($lines.Count -ne 9 -or $lines[0] -cne 'receipt_version=1' -or $lines[1] -cnotmatch '^root_pid=[1-9][0-9]*$' -or
      $lines[2] -cne "root_exit=$Exit" -or $lines[3] -cne 'timeout=0 active=0' -or
      $lines[4] -cnotmatch '^handles_before=[0-9]+ handles_after=[0-9]+ measured=[01]$' -or
      $lines[5] -cne 'owned_opened=6 owned_closed=6 close_failed=0' -or $lines[6] -cne 'handles_complete=1' -or
      $lines[7] -cne "final_result=$Exit" -or $lines[8] -cne 'proof_complete=1') { throw 'Invalid outer receipt' }
  return $lines
}
function Invoke-Helper($StartInfo, $StdoutPath, $StderrPath) {
  $out = [IO.File]::Open($StdoutPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
  try {
    $err = [IO.File]::Open($StderrPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try {
      $process = [Diagnostics.Process]::new(); $process.StartInfo = $StartInfo
      $clock = [Diagnostics.Stopwatch]::StartNew(); $started = [DateTime]::UtcNow
      try {
        if (!$process.Start()) { throw 'Helper start failed' }
        $pidValue = $process.Id; $null = $process.Handle
        $outTask = $process.StandardOutput.BaseStream.CopyToAsync($out)
        $errTask = $process.StandardError.BaseStream.CopyToAsync($err)
        $attempt = [ordered]@{pid=$pidValue;started_utc=$started.ToString('o');source=$ReviewedSourceSHA;
          helper=$helper;arguments=$arguments;controller_sha256=(Get-FileHash -LiteralPath $PSCommandPath).Hash.ToLowerInvariant()}
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes((ConvertTo-Json -InputObject $attempt -Depth 5) + "`n")
        $attemptFile = [IO.File]::Open((Join-Path $evidence 'attempt.json'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        try { $attemptFile.Write($bytes,0,$bytes.Length); $attemptFile.Flush($true) } finally { $attemptFile.Dispose() }
        $finished = $false
        while ($clock.ElapsedMilliseconds -lt 200000) {
          if ($outTask.IsFaulted -or $errTask.IsFaulted) { throw 'Log drain failed; stop' }
          $slice = [Math]::Min(200, 200000 - [int]$clock.ElapsedMilliseconds)
          if ($process.WaitForExit($slice)) { $finished = $true; break }
        }
        if (!$finished) { throw 'Helper timeout; job settlement unknown' }
        $remaining = [Math]::Max(0, 210000 - [int]$clock.ElapsedMilliseconds)
        if (![Threading.Tasks.Task]::WaitAll(@($outTask,$errTask),$remaining)) { throw 'Log drains unsettled' }
        $observed = [ordered]@{pid=$pidValue;exit=$process.ExitCode;started_utc=$started.ToString('o');
          ended_utc=[DateTime]::UtcNow.ToString('o');elapsed_ms=$clock.ElapsedMilliseconds}
        $out.Flush($true); $err.Flush($true)
        return $observed
      } finally {
        try {
          if (!$process.HasExited) {
            $process.Kill()
            $remaining = [Math]::Max(0, 210000 - [int]$clock.ElapsedMilliseconds)
            if (!$process.WaitForExit($remaining)) { throw 'Helper unsettled during cleanup' }
            throw 'Helper interrupted; job settlement unknown'
          }
        } finally { $process.Dispose() }
      }
    } finally { $err.Dispose() }
  } finally { $out.Dispose() }
}
Assert-NoReparseParent $evidence
$before = Assert-Inputs
if ((Get-PSDrive -Name ([IO.Path]::GetPathRoot($evidence).Substring(0,1))).Free -lt 20GB) { throw 'Insufficient storage; stop' }
New-Item -ItemType Directory -Path $evidence | Out-Null
foreach ($name in @('cases','temp','local','roaming','profile')) { New-Item -ItemType Directory -Path (Join-Path $evidence $name) | Out-Null }
$receipt = Join-Path $evidence 'outer.receipt'
$arguments = @($receipt,'180000',$PSScriptRoot,$python,'-B','-m','unittest','-v','test_build_only_native')
$start = [Diagnostics.ProcessStartInfo]::new()
$start.FileName = $helper; $start.WorkingDirectory = $PSScriptRoot; $start.UseShellExecute = $false
$start.CreateNoWindow = $true; $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
$start.RedirectStandardOutput = $true; $start.RedirectStandardError = $true
foreach ($value in $arguments) { $start.ArgumentList.Add($value) }
$start.Environment['BUILD_ONLY_NATIVE_QUALIFY'] = '1'; $start.Environment['BUILD_ONLY_NATIVE_DIAGNOSTIC'] = 'residual'
$start.Environment['BUILD_ONLY_NATIVE_ROOT'] = Join-Path $evidence 'cases'; $start.Environment['BUILD_ONLY_NATIVE_HELPER'] = $helper
$start.Environment['TEMP'] = Join-Path $evidence 'temp'; $start.Environment['TMP'] = $start.Environment['TEMP']
$start.Environment['TMPDIR'] = $start.Environment['TEMP']; $start.Environment['LOCALAPPDATA'] = Join-Path $evidence 'local'
$start.Environment['APPDATA'] = Join-Path $evidence 'roaming'; $start.Environment['USERPROFILE'] = Join-Path $evidence 'profile'
$start.Environment['PYTHONDONTWRITEBYTECODE'] = '1'; $null = $start.Environment.Remove('PYTHONPATH'); $null = $start.Environment.Remove('PYTHONHOME')
$observed = Invoke-Helper $start (Join-Path $evidence 'stdout.log') (Join-Path $evidence 'stderr.log')
$issues = [Collections.Generic.List[string]]::new()
if ($observed.exit -notin @(0,1)) { $issues.Add("Unexpected helper exit $($observed.exit)") }
$lines = @()
try { $lines = @(Assert-Receipt $receipt $observed.exit) }
catch { $issues.Add("Outer receipt: $($_.Exception.Message)") }
$log = [IO.File]::ReadAllText((Join-Path $evidence 'stderr.log'))
$innerLogPath = Join-Path $evidence 'cases/residual/helper.stderr'
$innerLog = if (Test-Path -LiteralPath $innerLogPath -PathType Leaf) { [IO.File]::ReadAllText($innerLogPath) } else { '' }
$fixtureReceipt = Join-Path $evidence 'cases/residual/build.receipt'
$receiptText = if (Test-Path -LiteralPath $fixtureReceipt -PathType Leaf) {
  if ((Get-Item -LiteralPath $fixtureReceipt).Length -gt 1048576) { throw 'Oversized fixture receipt' }
  [IO.File]::ReadAllText($fixtureReceipt)
} else { '' }
if (($log + $innerLog + $receiptText) -match '(?i)no space left|disk full|not enough (space|storage)|quota exceeded|errno 28|winerror 112') {
  throw 'Storage failure reported; stop dependent writes'
}
if ($log -notmatch '(?m)^Ran 1 test in [0-9.]+s\r?$' -or $log -match '(?im)\bskipped\b') {
  $issues.Add('Native diagnostic test summary missing or skipped')
}
$fixtureManifest = Join-Path $evidence 'cases/residual/manifest.json'
$fixtureOutput = Join-Path $evidence 'cases/residual/result.bin'
$fixtureManifestHash = if (Test-Path -LiteralPath $fixtureManifest -PathType Leaf) { (Get-FileHash -LiteralPath $fixtureManifest).Hash.ToLowerInvariant() } else { $null }
$fixtureReceiptHash = if (Test-Path -LiteralPath $fixtureReceipt -PathType Leaf) { (Get-FileHash -LiteralPath $fixtureReceipt).Hash.ToLowerInvariant() } else { $null }
$fixtureOutputHash = if (Test-Path -LiteralPath $fixtureOutput -PathType Leaf) { (Get-FileHash -LiteralPath $fixtureOutput).Hash.ToLowerInvariant() } else { $null }
if (!$fixtureManifestHash -or !$fixtureReceiptHash) { $issues.Add('Residual fixture evidence incomplete') }
$after = Assert-Inputs
$outerHash = if (Test-Path -LiteralPath $receipt -PathType Leaf) { (Get-FileHash -LiteralPath $receipt).Hash.ToLowerInvariant() } else { $null }
$record = [ordered]@{classification='residual-only diagnostic; not qualification';issues=$issues.ToArray();source_before=$before;source_after=$after;
  manifest_path=$PackageManifestPath;manifest_sha256=$PackageManifestSHA256.ToLowerInvariant();controller_sha256=(Get-FileHash -LiteralPath $PSCommandPath).Hash.ToLowerInvariant();
  helper=$helper;python=$python;helper_sha256=$helperHash.ToLowerInvariant();python_sha256=$pythonHash.ToLowerInvariant();
  arguments=$arguments;selector='residual';process=$observed;outer_receipt=$lines;
  stdout_sha256=(Get-FileHash -LiteralPath (Join-Path $evidence 'stdout.log')).Hash.ToLowerInvariant();
  stderr_sha256=(Get-FileHash -LiteralPath (Join-Path $evidence 'stderr.log')).Hash.ToLowerInvariant();
  outer_receipt_sha256=$outerHash;fixture_manifest=$fixtureManifest;fixture_manifest_sha256=$fixtureManifestHash;
  fixture_receipt=$fixtureReceipt;fixture_receipt_sha256=$fixtureReceiptHash;fixture_output=$fixtureOutput;
  fixture_output_sha256=$fixtureOutputHash}
$data = [Text.UTF8Encoding]::new($false).GetBytes((ConvertTo-Json -InputObject $record -Depth 8) + "`n")
$output = [IO.File]::Open((Join-Path $evidence 'diagnostic.json'),[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try { $output.Write($data,0,$data.Length); $output.Flush($true) } finally { $output.Dispose() }
Write-Output "Residual diagnostic only: outer_exit=$($observed.exit), source=$after; inspect fixture evidence"
if ($issues.Count -or $observed.exit -ne 0) { exit 1 }
