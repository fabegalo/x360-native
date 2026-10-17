# Optional Windows GPU counter sidecar. Never interprets engine utilization as
# per-frame GPU time; keep this raw evidence separate from native frame CSVs.
# Run only against the PID owned by the corresponding bounded benchmark.
param(
 [Parameter(Mandatory)][int]$ProcessId,
 [Parameter(Mandatory)][string]$Output,
 [ValidateRange(0,280)][double]$StartSeconds=240,
 [ValidateRange(1,30)][double]$DurationSeconds=30
)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $Output){throw 'Refusing to overwrite GPU evidence'}
$process=Get-Process -Id $ProcessId -ErrorAction Stop
$origin=$process.StartTime.ToUniversalTime()
$clock=[Diagnostics.Stopwatch]::StartNew()
$initialAge=([DateTime]::UtcNow-$origin).TotalSeconds
$samples=[Collections.Generic.List[object]]::new()
$failure=$null
try {
 while($initialAge+$clock.Elapsed.TotalSeconds -lt $StartSeconds+$DurationSeconds) {
  if($process.HasExited){throw 'Target exited before GPU collection completed'}
  $age=$initialAge+$clock.Elapsed.TotalSeconds
  if($age -ge $StartSeconds){
   $memory=@(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUProcessMemory -OperationTimeoutSec 2 |
     Where-Object Name -like "pid_${ProcessId}_*" | Select-Object Name,DedicatedUsage,SharedUsage,TotalCommitted)
   $engines=@(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine -OperationTimeoutSec 2 |
     Where-Object Name -like "pid_${ProcessId}_*" | Select-Object Name,UtilizationPercentage)
   $samples.Add(@{seconds=$age;query_end_seconds=$initialAge+$clock.Elapsed.TotalSeconds;memory=$memory;engines=$engines})
  }
  Start-Sleep -Milliseconds 1000
 }
}catch{$failure=$_.Exception.Message}
@{schema=1;pid=$ProcessId;process_start_utc=$origin.ToString('o');start_seconds=$StartSeconds;
 duration_seconds=$DurationSeconds;failure=$failure;samples=@($samples.ToArray());
 source='Windows GPU Performance Counters, per PID and adapter/engine';
 limitations='Sampled WDDM dedicated/shared usage and engine utilization; not GPU timestamp queries, residency proof, scanout or GPU ms/frame. Queries are sequential and asynchronously refreshed by Windows.'} |
 ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Output -Encoding utf8
if($failure){throw $failure}
