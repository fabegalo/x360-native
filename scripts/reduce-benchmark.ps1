# Reprocess preserved evidence without launching a title or modifying its CSV.
#requires -Version 7.0
param([Parameter(Mandatory)][string]$Run)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/benchmark-summary.ps1"
. "$PSScriptRoot/benchmark-process-summary.ps1"
$dir=(Resolve-Path -LiteralPath $Run).Path
$envFile=Join-Path $dir 'environment.json'
$envData=Get-Content $envFile -Raw | ConvertFrom-Json -AsHashtable
$resultFile=Join-Path $dir 'result.json'
$initialFile=Join-Path $dir 'result.initial.json'
$start=[double]$envData.scenario.start_seconds
$duration=[double]$envData.scenario.duration_seconds
$samples=Get-Content (Join-Path $dir 'process-samples.json') -Raw | ConvertFrom-Json
if($envData.measurement_clock) {
 $external=Get-BenchmarkProcessSummary $samples $start $duration $envData.hardware.logical_processors
 if($envData.instrumentation -eq 'off') {
  if(Test-Path (Join-Path $dir 'frames.csv')){throw 'OFF unexpectedly produced a frame CSV'}
  $summary=[ordered]@{schema=1;window_complete=$external.external_window_complete;unavailable=@{};counters=$null}
  foreach($metric in @('fps_mean','fps_1pct_low','frame_ms_mean','frame_ms_p95','frame_ms_p99','process_cpu_ms_per_frame','runtime_to_first_swap_ms')) {
   $summary[$metric]=$null;$summary.unavailable[$metric]='Instrumentation OFF; no independent frame provider installed'
  }
 } elseif($envData.instrumentation -eq 'on' -and $null -ne $envData.runtime_origin_approx_ms) {
  $summary=Get-BenchmarkSummary (Join-Path $dir 'frames.csv') ($start-$envData.runtime_origin_approx_ms/1000) $duration $envData.hardware.logical_processors
 } else {throw 'Invalid instrumentation mode or runtime origin'}
 foreach($key in $external.Keys){$summary[$key]=$external[$key]}
 $summary['instrumentation']=$envData.instrumentation
 $summary['gpu_ms_per_frame']=$null;$summary['vram_peak_bytes']=$null
 $summary.unavailable['gpu_ms_per_frame']='No validated per-process GPU timing provider'
 $summary.unavailable['vram_peak_bytes']='No validated per-process VRAM provider'
} else {
 $summary=Get-BenchmarkSummary (Join-Path $dir 'frames.csv') $start $duration $envData.hardware.logical_processors
}
if(!(Test-Path $initialFile)){Copy-Item $resultFile $initialFile}
$initial=Get-Content $initialFile -Raw | ConvertFrom-Json -AsHashtable
if(!$envData.measurement_clock) {
$selected=@($samples | Where-Object {$null -ne $_.runtime_seconds -and $_.runtime_seconds -ge $start -and $_.runtime_seconds -le $start+$duration})
$summary['ram_working_set_peak_bytes']=($selected.working_set_bytes | Measure-Object -Maximum).Maximum
$summary['private_commit_peak_bytes']=($selected.private_bytes | Measure-Object -Maximum).Maximum
if(!$selected.Count) {
 $summary.unavailable['ram_working_set_peak_bytes']='No process samples in measurement window'
 $summary.unavailable['private_commit_peak_bytes']='No process samples in measurement window'
}
}
$summary['scene_verified']=$false
foreach($name in @('title','label','termination','scene_verified','scene_review','calibration_only')) {
 if($envData.ContainsKey($name)){$summary[$name]=$envData[$name]}
}
$summary['log_counts']=@{error=0;warning=0;critical=0}
$summary['log_count_scope']='Retained rex*.log files only; rotation may discard earlier messages'
foreach($log in Get-ChildItem $dir -Filter 'rex*.log') {
 foreach($level in @('error','warning','critical')){$summary.log_counts[$level]+=@(Select-String -LiteralPath $log.FullName -Pattern "\[$level\]").Count}
}
$reduction=@{
 utc=[DateTime]::UtcNow.ToString('o')
 script_sha256=(Get-FileHash $PSCommandPath).Hash.ToLower()
 summary_sha256=(Get-FileHash "$PSScriptRoot/benchmark-summary.ps1").Hash.ToLower()
 raw_csv_sha256=if(Test-Path (Join-Path $dir 'frames.csv')){(Get-FileHash (Join-Path $dir 'frames.csv')).Hash.ToLower()}else{$null}
 process_samples_sha256=(Get-FileHash (Join-Path $dir 'process-samples.json')).Hash.ToLower()
 process_summary_sha256=(Get-FileHash "$PSScriptRoot/benchmark-process-summary.ps1").Hash.ToLower()
 initial_measurement_error=$initial.measurement_error
}
$summary['reduction']=$reduction;$envData['reduction']=$reduction
$summary | ConvertTo-Json -Depth 20 | Set-Content $resultFile -Encoding utf8
$envData | ConvertTo-Json -Depth 20 | Set-Content $envFile -Encoding utf8
$summary | ConvertTo-Json -Depth 20
