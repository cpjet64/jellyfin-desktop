param(
  [Parameter(Mandatory)] [string] $SourceSha,
  [Parameter(Mandatory)] [string] $TargetsSha,
  [Parameter(Mandatory)] [string] $CasesSha
)
$ErrorActionPreference = 'Stop'
$root = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/integration'
$proof = "$root/deps/w1224-proof"
$build = "$root/deps/w1192-healthy-build"
if ($SourceSha -cne 'b3986ab35cca5bbd0e3a723b353daef20059e47d' -or (git -C $root rev-parse HEAD) -ne $SourceSha) { throw 'source mismatch' }
if ($LASTEXITCODE -ne 0) { throw 'source lookup failed' }
$changes = @(git -C $root diff --name-only 94d31a9553ae55dffda4f13f5f61ecf3c8e0c9c3 $SourceSha)
if ($LASTEXITCODE -ne 0 -or $changes.Count -ne 1 -or $changes[0] -ne 'CMakeModules/HealthyRetirementTests.cmake') {
  throw 'unexpected changes to source inputs'
}
$status = @(git -C $root status --porcelain --untracked-files=all --ignore-submodules=all)
if ($LASTEXITCODE -ne 0 -or $status.Count) { throw 'source worktree not clean' }
$expectedMpvQt = git -C $root rev-parse "${SourceSha}:external/mpvqt"
if ($LASTEXITCODE -ne 0) { throw 'submodule pin unavailable' }
$actualMpvQt = git -C "$root/external/mpvqt" rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $actualMpvQt -ne $expectedMpvQt) { throw 'submodule pin mismatch' }
$subStatus = @(git -C "$root/external/mpvqt" status --porcelain --untracked-files=all)
if ($LASTEXITCODE -ne 0 -or $subStatus.Count) { throw 'submodule not clean' }
if (!(Test-Path -LiteralPath "$build/CMakeCache.txt")) { throw 'owned build cache missing' }
foreach ($item in @(@('targets.txt',$TargetsSha),@('cases.txt',$CasesSha))) {
  if ($item[1] -notmatch '^[0-9A-Fa-f]{64}$' -or (Get-FileHash -LiteralPath "$proof/$($item[0])").Hash -ne $item[1]) {
    throw "inventory hash mismatch: $($item[0])"
  }
}
$targets = @(Get-Content -LiteralPath "$proof/targets.txt")
$cases = @(Get-Content -LiteralPath "$proof/cases.txt")
if ($TargetsSha -ne 'FA5E2A4966445A3C656C48C1B82C6CC10E57F610B20FB0C4255DA02A7FAC1D62' -or
    $CasesSha -ne '479EA88352435EAF24D079A3C5376E8DD2A7247E2D565E488E228E60447D2731') { throw 'unapproved inventory' }
if ($targets.Count -ne 55 -or $cases.Count -ne 224 -or @($targets | Sort-Object -Unique).Count -ne $targets.Count -or
    @($cases | Sort-Object -Unique).Count -ne $cases.Count) { throw 'empty or duplicate inventory' }
if (@($targets | Where-Object { $_ -notmatch '^[A-Za-z0-9_.-]+$' }).Count -or
    @($cases | Where-Object { $_ -notmatch '^[A-Za-z0-9_.:-]+$' }).Count) { throw 'invalid inventory item' }
if ('test_healthy_restart' -notin $targets -or @($cases | Where-Object { $_ -match '^(test_healthy_restart|healthy_mpvqt_event_ownership)(\.|$)' }).Count) {
  throw 'held or native case in inventory'
}
if (@($cases | Where-Object { $_ -in @('healthy_graph_dispatch','healthy_graph_capture_lifetime',
    'healthy_b_channel_dispatch','healthy_graph_arm','healthy_graph_failure_provenance') }).Count) {
  throw 'unsplit case in inventory'
}
if (Test-Path -LiteralPath "$proof/profiles") { throw 'profile path already exists' }
New-Item -ItemType Directory -Path "$proof/profiles" | Out-Null
$vcvars = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat'
foreach ($name in @('PATH','INCLUDE','LIB','LIBPATH')) {
  $value = & cmd.exe /d /v:on /c "call `"$vcvars`" >nul && echo !$name!"
  if ($LASTEXITCODE -ne 0 -or -not $value) { throw "MSVC environment failed: $name" }
  [Environment]::SetEnvironmentVariable($name, ($value -join "`n"), 'Process')
}
$qt = 'E:/CursorAI/JellyfinDesktop/dev/windows/deps/qt/6.9.3/msvc2022_64'
$mpv = 'E:/CursorAI/JellyfinDesktop/dev/windows/deps/mpv'
$env:PATH = "$qt/bin;$mpv;$env:PATH"
$env:HEALTHY_TEST_SOURCE = $root; $env:MPVQT_TEST_SOURCE = "$root/external/mpvqt"
$env:QT_QPA_PLATFORM = 'offscreen'
Set-Location $root
function Run-Check($label, $program, $arguments, $limitMs, [bool] $allowFailure) {
  if ((Get-PSDrive E).Free -lt 20GB) { throw 'insufficient storage; stop' }
  $helper = "$root/deps/w1214-proof/job_launch_prod.exe"
  if ((Get-FileHash -LiteralPath $helper).Hash -ne '7AE01DFD262A18BF863C057D570993B2C4DC529FCC03AC1E58D15F0DEDB0EBD5') {
    throw 'job launcher identity mismatch'
  }
  $executable = (Get-Command $program -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
  if (!$executable -or ![IO.Path]::IsPathFullyQualified($executable) -or !(Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "executable unavailable: $program"
  }
  $receipt = "$proof/$label.receipt"
  if (Test-Path -LiteralPath $receipt) { throw 'receipt already exists' }
  $jobArgs = @($receipt, [string]$limitMs, $root, $executable) + $arguments
  # This gate's fixed arguments contain no embedded quotes or trailing backslashes.
  if (@($jobArgs | Where-Object { $_ -match '"|\\$' }).Count) { throw 'unsupported job argument' }
  $quoted = @($jobArgs | ForEach-Object { '"' + ($_ -replace '"', '\"') + '"' })
  $proc = Start-Process -FilePath $helper -ArgumentList $quoted -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput "$proof/$label.stdout.log" -RedirectStandardError "$proof/$label.stderr.log"
  try {
  $null = $proc.Handle
  if (!$proc.WaitForExit($limitMs + 30000)) {
    $proc.Kill()
    if (!$proc.WaitForExit(10000)) { throw "$label launcher unsettled" }
    throw "$label launcher timed out; job settlement unknown"
  }
  $proc.WaitForExit()
  $exit = $proc.ExitCode
  [IO.File]::AppendAllText("$proof/results.txt", "$label launcher_exit=$exit pid=$($proc.Id)`n")
  if (($exit -ne 0 -and !($allowFailure -and $exit -eq 8)) -or !(Test-Path -LiteralPath $receipt -PathType Leaf)) { throw "$label launcher failed" }
  $receiptFile = Get-Item -LiteralPath $receipt -Force
  if ($receiptFile.Length -gt 1024 -or $receiptFile.Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw "$label unsafe job receipt"
  }
  $lines = @(Get-Content -LiteralPath $receipt)
  if ($lines.Count -ne 9 -or $lines[0] -cne 'receipt_version=1' -or $lines[1] -notmatch '^root_pid=[1-9][0-9]*$' -or
      $lines[2] -cne "root_exit=$exit" -or $lines[3] -cne 'timeout=0 active=0' -or
      $lines[4] -notmatch '^handles_before=[0-9]+ handles_after=[0-9]+ measured=[01]$' -or
      $lines[5] -cne 'owned_opened=6 owned_closed=6 close_failed=0' -or $lines[6] -cne 'handles_complete=1' -or
      $lines[7] -cne "final_result=$exit" -or $lines[8] -cne 'proof_complete=1') { throw "$label invalid job receipt" }
  return $exit
  } finally {
    try {
      if (!$proc.HasExited) {
        $proc.Kill()
        if (!$proc.WaitForExit(10000)) { throw "$label launcher unsettled; job settlement unknown" }
        throw "$label launcher interrupted; job settlement unknown"
      }
    } finally { $proc.Dispose() }
  }
}
function Get-CopySnapshot {
  $copy = "$build/healthy-restart-copy"
  if (!(Test-Path -LiteralPath $copy -PathType Container) -or
      @(@(Get-Item -LiteralPath $copy -Force) + @(Get-ChildItem -LiteralPath $copy -Force -Recurse) |
        Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }).Count) { throw 'unsafe copied tree' }
  foreach ($file in Get-ChildItem -LiteralPath $copy -Recurse -Force -File | Sort-Object FullName) {
    $relative = $file.FullName.Substring($copy.Replace('/','\').Length + 1)
    "$( (Get-FileHash -LiteralPath $file.FullName).Hash )`t$($file.Length)`t$relative"
  }
}
function Set-CaseProfile($label) {
  $profile = "$proof/profiles/$label"
  if (Test-Path -LiteralPath $profile) { throw 'profile already exists' }
  [IO.Directory]::CreateDirectory($profile) | Out-Null
  foreach ($name in @('temp','local','roaming','owned')) {
    New-Item -ItemType Directory -Path "$profile/$name" | Out-Null
  }
  $env:TEMP = "$profile/temp"; $env:TMP = "$profile/temp"
  $env:APPDATA = "$profile/roaming"; $env:LOCALAPPDATA = "$profile/local"
  $env:JELLYFIN_TEST_OWNED_ROOT = "$profile/owned"
}
function Get-BinarySnapshot {
  foreach ($target in $targets) {
    $path = "$build/tests/$target.exe"
    $item = Get-Item -LiteralPath $path -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'unsafe binary' }
    "$( (Get-FileHash -LiteralPath $path).Hash )`t$($item.Length)`t$path"
  }
  foreach ($path in @("$build/src/HealthyRestartCore.lib", "$build/external/HealthyRestartMpvQt.lib",
      "$build/tests/HealthyGraphTestCore.lib", "$build/tests/HealthyGraphTestMpvQt.lib")) {
    $item = Get-Item -LiteralPath $path -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'unsafe library' }
    "$( (Get-FileHash -LiteralPath $path).Hash )`t$($item.Length)`t$path"
  }
}
try {
  $before = @(Get-CopySnapshot)
  if (!$before.Count) { throw 'empty copied tree' }
  [IO.File]::WriteAllLines("$proof/copied-before.tsv", $before)
  $previous = @([IO.File]::ReadAllLines("$root/deps/w1220-proof/copied-after.tsv"))
  if ($previous.Count -ne $before.Count -or (Compare-Object $previous $before -SyncWindow 0)) { throw 'copied tree differs from W1220' }
  $binaries = @(Get-BinarySnapshot)
  [IO.File]::WriteAllLines("$proof/binaries-before.tsv", $binaries)
  $held = "$build/tests/test_healthy_restart.exe"
  if ((Get-FileHash -LiteralPath $held).Hash -ne '69250F8A6B27917023A549FFCADFD2EC514FD2CDDFC59D597F24F97F4758775E') {
    throw 'held binary identity mismatch'
  }
  Set-CaseProfile 'inventory'
  $inventoryResult = Run-Check 'inventory' 'ctest.exe' @('--test-dir',$build,'--show-only=json-v1') 60000 $false
  if ($inventoryResult -ne 0) { throw 'inventory failed' }
  $inventory = Get-Content -LiteralPath "$proof/inventory.stdout.log" -Raw | ConvertFrom-Json
  $selected = @($cases | Select-Object -Skip 52)
  if ($selected.Count -ne 172) { throw 'survey count mismatch' }
  $index = 52; $failures = 0
  foreach ($case in $selected) {
    $index++
    $label = 'case-{0:D4}' -f $index
    [IO.File]::AppendAllText("$proof/case-map.txt", "$label`t$case`n")
    $matching = @($inventory.tests | Where-Object { $_.name -ceq $case })
    $target = ($case -split '\.',2)[0]
    $executable = [IO.Path]::GetFullPath("$build/tests/$target.exe")
    if ($matching.Count -ne 1 -or [IO.Path]::GetFullPath($matching[0].command[0]) -ine $executable) { throw "case executable mismatch: $case" }
    $binaryBefore = (Get-FileHash -LiteralPath $executable).Hash
    $baseline = @($binaries | Where-Object { $_.EndsWith("`t$build/tests/$target.exe") })
    if ($baseline.Count -ne 1 -or !$baseline[0].StartsWith("$binaryBefore`t")) { throw "binary drift: $case" }
    Set-CaseProfile $label
    $pattern = '^' + [regex]::Escape($case) + '$'
    $report = "$proof/$label.xml"
    $exit = Run-Check $label 'ctest.exe' @('--test-dir',$build,'-V','-R',$pattern,'--no-tests=error',
      '--output-on-failure','--timeout','60','--output-junit',$report) 90000 $true
    if ((Get-FileHash -LiteralPath $executable).Hash -ne $binaryBefore) { throw "binary changed during case: $case" }
    [xml] $xml = [IO.File]::ReadAllText($report)
    $suite = $xml.testsuite
    $entries = @($suite.testcase)
    $expectedFailures = if ($exit -eq 8) { '1' } else { '0' }
    $expectedStatus = if ($exit -eq 8) { 'fail' } else { 'run' }
    if ($suite.tests -ne '1' -or $suite.failures -ne $expectedFailures -or $suite.disabled -ne '0' -or
        $suite.skipped -ne '0' -or $entries.Count -ne 1 -or $entries[0].name -ne $case -or
        $entries[0].status -ne $expectedStatus -or $entries[0].skipped -or $entries[0].error -or
        (($exit -eq 8) -ne [bool]$entries[0].failure)) { throw "invalid case report: $case" }
    if ($exit -eq 8) {
      $output = [IO.File]::ReadAllText("$proof/$label.stdout.log")
      if ($entries[0].failure.message -cne 'Failed' -or $output -notmatch '\*\*\*Failed' -or
          $output -match '\*\*\*(Exception|Timeout|Not Run)' -or
          $output -notmatch '(?m)^\s*\d+: .*?(FAIL!|check failed at line)') { throw "ambiguous failure: $case" }
      $failures++
      [IO.File]::AppendAllText("$proof/failures.txt", "$index`t$case`n")
    }
  }
  $after = @(Get-CopySnapshot); $afterBinaries = @(Get-BinarySnapshot)
  [IO.File]::WriteAllLines("$proof/copied-after.tsv", $after)
  [IO.File]::WriteAllLines("$proof/binaries-after.tsv", $afterBinaries)
  if ((Compare-Object $before $after -SyncWindow 0) -or (Compare-Object $binaries $afterBinaries -SyncWindow 0)) { throw 'source or binaries changed' }
  [IO.File]::AppendAllText("$proof/results.txt", "survey_cases=$($selected.Count) failures=$failures`n")
} finally {
  $end = git -C $root rev-parse HEAD
  if ($LASTEXITCODE -ne 0) { throw 'final source lookup failed' }
  $finalStatus = @(git -C $root status --porcelain --untracked-files=all --ignore-submodules=all)
  if ($LASTEXITCODE -ne 0 -or $finalStatus.Count -or $end -ne $SourceSha) { throw 'final source state mismatch' }
  $finalMpvQt = git -C "$root/external/mpvqt" rev-parse HEAD
  if ($LASTEXITCODE -ne 0 -or $finalMpvQt -ne $expectedMpvQt) { throw 'final submodule pin mismatch' }
  $finalSubStatus = @(git -C "$root/external/mpvqt" status --porcelain --untracked-files=all)
  if ($LASTEXITCODE -ne 0 -or $finalSubStatus.Count) { throw 'final submodule not clean' }
  [IO.File]::AppendAllText("$proof/results.txt", "start=$SourceSha end=$end`n")
}
