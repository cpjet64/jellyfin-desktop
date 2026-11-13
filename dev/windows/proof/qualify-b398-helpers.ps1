param([Parameter(Mandatory)][string]$RunId)
$ErrorActionPreference = 'Stop'
$root = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/integration'
$sourceRoot = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/w1218-proof-tooling'
$base = "$root/deps/reconcile-20261113/proof-qualification"
$old = "$root/deps/w1214-proof"
$testHash = 'F8742455D01B0BC0AD6ABF1C0AF98EE450B7296F5B674B4BBD5B5EF21B944E07'
$cppHash = 'A05BC5DE7EB10D0896375C781C2DE4F3A9AFCA888AF3B292D45B4C6D8943150B'
$prodHash = '7AE01DFD262A18BF863C057D570993B2C4DC529FCC03AC1E58D15F0DEDB0EBD5'
$testExeHash = '3CEDDCE12509CEB2681712F50A540107E944102BD6A1083FCB68B80EFA9C000D'
$head = 'b3986ab35cca5bbd0e3a723b353daef20059e47d'
if ($RunId -notmatch '^[A-Za-z0-9_-]{1,40}$') { throw 'Invalid run ID' }
$run = "$base/$RunId"
if (Test-Path -LiteralPath $run) { throw 'Run already exists' }
$python = (Get-Command python.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
if (![IO.Path]::IsPathFullyQualified($python)) { throw 'Python is not absolute' }
$testSource = "$sourceRoot/dev/windows/proof/test_job_launch.py"
$cpp = "$sourceRoot/dev/windows/proof/job_launch.cpp"
function Check-Hash($Path, $Expected) {
  if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -cne $Expected) { throw "Hash mismatch: $Path" }
}
function Check-Source {
  $actual = & git.exe -C $root rev-parse HEAD
  if ($LASTEXITCODE -ne 0 -or $actual -cne $head) { throw 'Integration HEAD mismatch' }
  $status = @(& git.exe -C $root status --porcelain --untracked-files=all --ignore-submodules=all)
  if ($LASTEXITCODE -ne 0 -or $status.Count) { throw 'Integration status unavailable or dirty' }
  $pin = & git.exe -C $root rev-parse "${head}:external/mpvqt"
  if ($LASTEXITCODE -ne 0) { throw 'Submodule pin unavailable' }
  $sub = "$root/external/mpvqt"
  $actual = & git.exe -C $sub rev-parse HEAD
  if ($LASTEXITCODE -ne 0 -or $actual -cne $pin) { throw 'Submodule revision mismatch' }
  $status = @(& git.exe -C $sub status --porcelain --untracked-files=all)
  if ($LASTEXITCODE -ne 0 -or $status.Count) { throw 'Submodule status unavailable or dirty' }
  $actual = & git.exe -C $sourceRoot rev-parse HEAD
  if ($LASTEXITCODE -ne 0 -or $actual -cne 'fcfd9072267b82d2325ea9c6f986ade00cf778e0') { throw 'Test source revision mismatch' }
  Check-Hash $testSource $testHash; Check-Hash $cpp $cppHash
  Check-Hash "$old/job_launch_prod.exe" $prodHash; Check-Hash "$old/job_launch.exe" $testExeHash
  return $pin
}
$common = @('test_immediate_success','test_nonzero','test_child_and_grandchild_timeout',
  'test_active_root_timeout','test_natural_grandchild_settlement','test_exited_root_snapshot_does_not_end_job_wait',
  'test_unrelated_process_survives_job_timeout','test_arguments_round_trip','test_existing_receipt_prevents_launch')
$hooks = @('test_assignment_failure_does_not_resume','test_early_close_never_passes')
function Invoke-Batch($Name, $Binary, $Hash, [string[]]$Names) {
  if ((Get-PSDrive E).Free -lt 20GB) { throw 'Insufficient storage; stop' }
  if (@($Names | Sort-Object -Unique).Count -ne $Names.Count) { throw 'Duplicate method' }
  $dir = "$run/$Name"; New-Item -ItemType Directory -Path $dir | Out-Null
  foreach ($part in @('temp','local','roaming','profile')) { New-Item -ItemType Directory -Path "$dir/$part" | Out-Null }
  Copy-Item -LiteralPath $testSource -Destination "$dir/test_job_launch.py"
  Copy-Item -LiteralPath $Binary -Destination "$dir/job_launch.exe"
  Check-Hash "$dir/test_job_launch.py" $testHash; Check-Hash "$dir/job_launch.exe" $Hash
  $env:TEMP="$dir/temp"; $env:TMP=$env:TEMP; $env:TMPDIR=$env:TEMP
  $env:LOCALAPPDATA="$dir/local"; $env:APPDATA="$dir/roaming"; $env:USERPROFILE="$dir/profile"
  $env:PYTHONDONTWRITEBYTECODE='1'
  Remove-Item Env:W1214_TEST_ASSIGN_FAIL,Env:W1214_TEST_EARLY_CLOSE -ErrorAction SilentlyContinue
  $methods = @($Names | ForEach-Object { "test_job_launch.JobLauncherTests.$_" })
  [IO.File]::WriteAllLines("$dir/methods.txt", $methods)
  $jobArgs = @("$dir/outer.receipt",'180000',$dir,$python,'-B','-m','unittest','-v') + $methods
  if (@($jobArgs | Where-Object { $_ -match '"|\\$' }).Count) { throw 'Unsupported argument' }
  $quoted = @($jobArgs | ForEach-Object { '"' + $_ + '"' })
  $proc = Start-Process -FilePath "$old/job_launch_prod.exe" -ArgumentList $quoted -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput "$dir/stdout.log" -RedirectStandardError "$dir/stderr.log"
  try {
    $null = $proc.Handle
    if (!$proc.WaitForExit(210000)) {
      $proc.Kill()
      if (!$proc.WaitForExit(10000)) { throw 'Launcher unsettled; job settlement unknown' }
      throw 'Launcher timed out; job settlement unknown'
    }
    $proc.WaitForExit(); $exit = $proc.ExitCode
    [IO.File]::WriteAllText("$dir/actual.txt", "launcher_pid=$($proc.Id)`nlauncher_exit=$exit`n")
    if ($exit -ne 0) { throw "$Name launcher failed" }
    $receipt = Get-Item -LiteralPath "$dir/outer.receipt" -Force
    if ($receipt.Length -gt 1024 -or $receipt.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Unsafe receipt' }
    $lines = @(Get-Content -LiteralPath $receipt.FullName)
    if ($lines.Count -ne 9 -or $lines[0] -cne 'receipt_version=1' -or $lines[1] -notmatch '^root_pid=[1-9][0-9]*$' -or
        $lines[2] -cne 'root_exit=0' -or $lines[3] -cne 'timeout=0 active=0' -or
        $lines[4] -notmatch '^handles_before=[0-9]+ handles_after=[0-9]+ measured=[01]$' -or
        $lines[5] -cne 'owned_opened=6 owned_closed=6 close_failed=0' -or $lines[6] -cne 'handles_complete=1' -or
        $lines[7] -cne 'final_result=0' -or $lines[8] -cne 'proof_complete=1') { throw 'Invalid receipt' }
    $log = [IO.File]::ReadAllText("$dir/stderr.log")
    foreach ($method in $Names) {
      $escaped = [regex]::Escape($method)
      if ([regex]::Matches($log, "(?m)^$escaped \(test_job_launch\.JobLauncherTests\.$escaped\) \.\.\. ok\r?$").Count -ne 1) {
        throw "Missing or duplicate test result: $method"
      }
    }
    if ($log -notmatch "(?m)^Ran $($Names.Count) tests? in [0-9.]+s\r?$" -or
        $log -notmatch '(?m)^OK\r?$' -or $log -match '(?im)\b(skip|skipped|FAIL|ERROR)\b') { throw 'Invalid unittest summary' }
    Check-Hash "$dir/test_job_launch.py" $testHash; Check-Hash "$dir/job_launch.exe" $Hash
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
$pin = Check-Source
New-Item -ItemType Directory -Path $run | Out-Null
try {
  Invoke-Batch 'production' "$old/job_launch_prod.exe" $prodHash $common
  Invoke-Batch 'testmode' "$old/job_launch.exe" $testExeHash ($common + $hooks)
} finally {
  $finalPin = Check-Source
  [IO.File]::WriteAllText("$run/source.txt", "head=$head`nsubmodule=$pin`nfinal_submodule=$finalPin`npython=$python`n")
}
