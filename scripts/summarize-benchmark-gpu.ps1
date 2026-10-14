# Reduce optional WDDM samples without interpreting utilization as GPU time.
param([Parameter(Mandatory)][string]$InputFile,
      [Parameter(Mandatory)][string]$Output)
$ErrorActionPreference='Stop'
$data=Get-Content -LiteralPath $InputFile -Raw | ConvertFrom-Json
$rows=@($data.samples | Where-Object {
 $_.seconds -ge $data.start_seconds -and
 $_.query_end_seconds -le $data.start_seconds+$data.duration_seconds
})
$engines=@($rows | ForEach-Object {$_.engines} | Group-Object Name | ForEach-Object {
 $stats=$_.Group | Measure-Object UtilizationPercentage -Average -Minimum -Maximum
 @{engine=($_.Name -replace '^pid_\d+_','');samples=$_.Count;
   utilization_percent_mean=$stats.Average;utilization_percent_min=$stats.Minimum;
   utilization_percent_max=$stats.Maximum}
})
$memory=@($rows | ForEach-Object {$_.memory} | Group-Object Name | ForEach-Object {
 @{adapter=($_.Name -replace '^pid_\d+_','');samples=$_.Count;
   dedicated_usage_peak_bytes=($_.Group | Measure-Object DedicatedUsage -Maximum).Maximum;
   shared_usage_peak_bytes=($_.Group | Measure-Object SharedUsage -Maximum).Maximum;
   total_committed_peak_bytes=($_.Group | Measure-Object TotalCommitted -Maximum).Maximum}
})
@{schema=1;raw_sha256=(Get-FileHash -LiteralPath $InputFile).Hash.ToLower();
 source=$data.source;limitations=$data.limitations;failure=$data.failure;
 start_seconds=$data.start_seconds;duration_seconds=$data.duration_seconds;
 samples=$rows.Count;boundary_excluded_samples=@($data.samples).Count-$rows.Count;
 engines=$engines;memory=$memory;gpu_ms_per_frame=$null} |
 ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Output -Encoding utf8
