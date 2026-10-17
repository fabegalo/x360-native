#requires -Version 7.0
# Explicit real-game workload, never invoked by CTest. Each child has a watchdog.
param(
 [Parameter(Mandatory)][ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Label,
 [ValidateSet('castle_crashers','forza_horizon')][string[]]$Titles=@('castle_crashers','forza_horizon'),
 [ValidateSet(4,8)][int]$Samples=4,
 [ValidateRange(1,8)][int]$FirstSample=1,
 [string]$BinaryRoot='.local/evidence/benchmarks/binaries/sdk72'
)
$ErrorActionPreference='Stop'
$root= (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Set-Location $root
if($FirstSample -gt $Samples){throw 'FirstSample must not exceed Samples'}
New-Item -ItemType Directory -Force (Join-Path $root '.local/evidence/logs') | Out-Null
$order=@('Off','On','On','Off','On','Off','Off','On')
foreach($title in $Titles) {
 for($i=$FirstSample;$i -le $Samples;$i++) {
  $mode=$order[$i-1];$runLabel='{0}-{1:00}-{2}' -f $Label,$i,$mode.ToLowerInvariant()
  $log=Join-Path $root ".local/evidence/logs/$runLabel-$title.log"
  Write-Output "START $title $i/$Samples $mode $([DateTime]::UtcNow.ToString('o'))"
  $si=[Diagnostics.ProcessStartInfo]::new()
  $si.FileName=(Get-Process -Id $PID).Path;$si.UseShellExecute=$false;$si.CreateNoWindow=$true
  $si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true
  foreach($arg in @('-NoProfile','-File',"$PSScriptRoot/benchmark-runtime.ps1",'-Title',$title,'-Label',$runLabel,'-Instrumentation',$mode,'-BinaryDirectory',(Join-Path $BinaryRoot $title))){$si.ArgumentList.Add($arg)}
  $p=[Diagnostics.Process]::Start($si)
  $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync()
  try {
   if(!$p.WaitForExit(330000)){throw "Benchmark watchdog expired: $title $runLabel"}
   $stdout.Result+$stderr.Result | Set-Content $log
   if($p.ExitCode -ne 0){throw "Benchmark failed ($($p.ExitCode)): $log"}
  } finally {
   if(!$p.HasExited){$p.Kill($true);[void]$p.WaitForExit(5000)}
   $p.Dispose()
  }
  Write-Output "DONE $title $i/$Samples $mode $([DateTime]::UtcNow.ToString('o'))"
 }
}
