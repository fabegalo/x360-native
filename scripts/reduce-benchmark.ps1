# Reprocess preserved evidence without launching a title or modifying its CSV.
#requires -Version 7.0
param([Parameter(Mandatory)][string]$Run)
$ErrorActionPreference='Stop'
. "$PSScriptRoot/benchmark-summary.ps1"
$dir=(Resolve-Path -LiteralPath $Run).Path
$envFile=Join-Path $dir 'environment.json'
$envData=Get-Content $envFile -Raw | ConvertFrom-Json -AsHashtable
$resultFile=Join-Path $dir 'result.json'
$initialFile=Join-Path $dir 'result.initial.json'
$start=[double]$envData.scenario.start_seconds
$duration=[double]$envData.scenario.duration_seconds
$summary=Get-BenchmarkSummary (Join-Path $dir 'frames.csv') $start $duration $envData.hardware.logical_processors
if(!(Test-Path $initialFile)){Copy-Item $resultFile $initialFile}
$initial=Get-Content $initialFile -Raw | ConvertFrom-Json -AsHashtable
$samples=Get-Content (Join-Path $dir 'process-samples.json') -Raw | ConvertFrom-Json
$selected=@($samples | Where-Object {$null -ne $_.runtime_seconds -and $_.runtime_seconds -ge $start -and $_.runtime_seconds -le $start+$duration})
$summary['ram_working_set_peak_bytes']=($selected.working_set_bytes | Measure-Object -Maximum).Maximum
$summary['private_commit_peak_bytes']=($selected.private_bytes | Measure-Object -Maximum).Maximum
if(!$selected.Count) {
 $summary.unavailable['ram_working_set_peak_bytes']='No process samples in measurement window'
 $summary.unavailable['private_commit_peak_bytes']='No process samples in measurement window'
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
 raw_csv_sha256=(Get-FileHash (Join-Path $dir 'frames.csv')).Hash.ToLower()
 initial_measurement_error=$initial.measurement_error
}
$summary['reduction']=$reduction;$envData['reduction']=$reduction
$summary | ConvertTo-Json -Depth 20 | Set-Content $resultFile -Encoding utf8
$envData | ConvertTo-Json -Depth 20 | Set-Content $envFile -Encoding utf8
$summary | ConvertTo-Json -Depth 20
