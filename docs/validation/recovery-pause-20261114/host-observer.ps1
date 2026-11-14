$ErrorActionPreference = 'Stop'
$root = 'E:/CursorAI/JellyfinDesktop/deps/reconcile-20261113/v2-pure-host-01'
$exe = 'C:/Program Files/WindowsApps/OpenAI.CodexPrimaryRuntime.v26-1112-611-0_26.1112.611.0_x64__3k8sg7r9htsxt/dependencies/native/powershell/pwsh.exe'
$script = 'E:/CursorAI/JellyfinDesktop/deps/worktrees/compiler-pure-tests-v2-20261114/dev/windows/proof/proposals/cl-service-v2/pure-tests/pure-controller.ps1'
$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName=$exe; $info.UseShellExecute=$false; $info.CreateNoWindow=$true
$info.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
$info.RedirectStandardOutput=$true; $info.RedirectStandardError=$true
foreach($arg in @('-NoLogo','-NoProfile','-NonInteractive','-WindowStyle','Hidden','-File',$script,
  '-ReviewedProposalSHA','241811d086205bf5f1ef8fe6282cfa5a2c75e545',
  '-ReviewedControllerSHA256','8e5ec3d69570253779396c122dde1887f946b4f78f279c556c28124fc03cd5f6')) { $info.ArgumentList.Add($arg) }
$out=[IO.File]::Open("$root/host.stdout",'CreateNew','Write','Read')
$err=[IO.File]::Open("$root/host.stderr",'CreateNew','Write','Read')
$proc=[Diagnostics.Process]::new(); $proc.StartInfo=$info
$record=[ordered]@{schema='pure-host-observation-v1'; started=$false; pid=$null; creation_utc=$null;
  exit=$null; timed_out=$false; settlement='not_started'; error=$null; host_disposed=$false}
try {
  if (!$proc.Start()) { throw 'Host failed to start' }
  $record.started=$true; $record.pid=$proc.Id; $record.creation_utc=$proc.StartTime.ToUniversalTime().ToString('o'); $null=$proc.Handle
  $a=$proc.StandardOutput.BaseStream.CopyToAsync($out); $b=$proc.StandardError.BaseStream.CopyToAsync($err)
  if (!$proc.WaitForExit(120000)) {
    $record.timed_out=$true; $record.settlement='helper and child settlement unknown'
    $proc.Kill(); if (!$proc.WaitForExit(5000)) { throw 'Host termination unresolved' }
  } else { $record.settlement='host exited; helper settlement requires receipt' }
  $record.exit=$proc.ExitCode
  if (![Threading.Tasks.Task]::WaitAll(@($a,$b),5000)) { throw 'Parent stream drains unresolved' }
  $out.Flush($true); $err.Flush($true)
} catch { $record.error=$_.ToString() }
finally {
  try { $proc.Dispose(); $record.host_disposed=$true } catch { $record.error="host disposal: $_" }
  try { $out.Dispose() } catch { $record.error="stdout disposal: $_" }
  try { $err.Dispose() } catch { $record.error="stderr disposal: $_" }
  [IO.File]::WriteAllText("$root/parent.json",(($record | ConvertTo-Json -Compress) + "`n"),[Text.UTF8Encoding]::new($false))
}
