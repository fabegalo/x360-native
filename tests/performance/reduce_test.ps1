#requires -Version 7.0
$ErrorActionPreference='Stop'
$temp=Join-Path ([IO.Path]::GetTempPath()) ('x360n-reduce-'+[guid]::NewGuid())
New-Item -ItemType Directory $temp | Out-Null
try {
 $fields=@('draw_calls','command_buffer_stalls','vertices_processed','functions_dispatched','interrupt_dispatches','texture_cache_hits','texture_cache_misses','pipeline_cache_hits','pipeline_cache_misses','shader_translation_us','shader_translations','pipeline_creation_us','pipeline_creations','swap_failures')
 0..100 | ForEach-Object {
  $row=[ordered]@{runtime_elapsed_us=$_*10000;process_cpu_us=$_*20000}
  foreach($name in $fields){$row[$name]=0};[pscustomobject]$row
 } | Export-Csv -NoTypeInformation "$temp/frames.csv"
 @{scenario=@{start_seconds=0;duration_seconds=1};hardware=@{logical_processors=4};title='fixture';scene_verified=$true;scene_review='fixture';calibration_only=$true} |
  ConvertTo-Json -Depth 8 | Set-Content "$temp/environment.json"
 @{measurement_error='original failure';window_complete=$false} | ConvertTo-Json | Set-Content "$temp/result.json"
 @(@{runtime_seconds=.5;working_set_bytes=123;private_bytes=456}) | ConvertTo-Json -AsArray | Set-Content "$temp/process-samples.json"
 $csvHash=(Get-FileHash "$temp/frames.csv").Hash
 $initialHash=(Get-FileHash "$temp/result.json").Hash
 $script=Join-Path $PSScriptRoot '../../scripts/performance/reduce-benchmark.ps1'
 1..2 | ForEach-Object {
  & $script -Run $temp | Out-Null
  $r=Get-Content "$temp/result.json" -Raw | ConvertFrom-Json
  if(!$r.window_complete -or !$r.scene_verified -or !$r.calibration_only -or $r.scene_review -ne 'fixture'){throw 'Review/calibration metadata lost'}
  if($r.reduction.initial_measurement_error -ne 'original failure'){throw 'Original reduction failure lost'}
  if($r.ram_working_set_peak_bytes -ne 123 -or $r.private_commit_peak_bytes -ne 456){throw 'Process sample reduction failed'}
  if((Get-FileHash "$temp/frames.csv").Hash -ne $csvHash -or (Get-FileHash "$temp/result.initial.json").Hash -ne $initialHash){throw 'Raw/initial evidence changed'}
 }
 '[]' | Set-Content "$temp/process-samples.json"
 & $script -Run $temp | Out-Null
 $r=Get-Content "$temp/result.json" -Raw | ConvertFrom-Json
 if($null -ne $r.ram_working_set_peak_bytes -or !$r.unavailable.ram_working_set_peak_bytes){throw 'Missing RAM fabricated'}
 $priorHash=(Get-FileHash "$temp/result.json").Hash
 'invalid' | Set-Content "$temp/frames.csv"
 $rejected=$false
 try {& $script -Run $temp | Out-Null}catch{$rejected=$true}
 if(!$rejected -or (Get-FileHash "$temp/result.json").Hash -ne $priorHash){throw 'Failed reduction replaced the result'}
 # P2 uses a common external clock in ON and OFF; native CSV origin differs.
 0..100 | ForEach-Object {
  $row=[ordered]@{runtime_elapsed_us=$_*10000;process_cpu_us=$_*20000}
  foreach($name in $fields){$row[$name]=0};[pscustomobject]$row
 } | Export-Csv -NoTypeInformation "$temp/frames.csv"
 $e=@{scenario=@{start_seconds=.1;duration_seconds=.8};hardware=@{logical_processors=4};title='fixture';scene_verified=$true;scene_review='fixture';measurement_clock='external';instrumentation='on';runtime_origin_approx_ms=100}
 $e | ConvertTo-Json -Depth 8 | Set-Content "$temp/environment.json"
 @(0..2 | ForEach-Object {@{seconds=$_;cpu_ms=$_*2000;working_set_bytes=123;private_bytes=456}}) | ConvertTo-Json | Set-Content "$temp/process-samples.json"
 # Interior RAM samples are necessary even with interpolable CPU boundaries.
 $e.scenario.duration_seconds=1.8
 $e | ConvertTo-Json -Depth 8 | Set-Content "$temp/environment.json"
 0..200 | ForEach-Object {
  $row=[ordered]@{runtime_elapsed_us=$_*10000;process_cpu_us=$_*20000}
  foreach($name in $fields){$row[$name]=0};[pscustomobject]$row
 } | Export-Csv -NoTypeInformation "$temp/frames.csv"
 & $script -Run $temp | Out-Null
 $r=Get-Content "$temp/result.json" -Raw | ConvertFrom-Json
 if(!$r.external_window_complete -or [Math]::Abs($r.external_process_cpu_ms-3600) -gt .001 -or [Math]::Abs($r.fps_mean-100) -gt .0001){throw 'ON external/native reduction mismatch'}
 Remove-Item -LiteralPath "$temp/frames.csv"
 $e.instrumentation='off';$e | ConvertTo-Json -Depth 8 | Set-Content "$temp/environment.json"
 & $script -Run $temp | Out-Null
 $r=Get-Content "$temp/result.json" -Raw | ConvertFrom-Json
 if(!$r.window_complete -or $r.external_process_cpu_ms -ne 3600 -or $null -ne $r.fps_mean -or $null -ne $r.reduction.raw_csv_sha256){throw 'OFF reduction fabricated frames or lost process CPU'}
 'Reduction: repeated processing, raw/initial preservation, review metadata and unavailable RAM passed'
} finally {
 Get-ChildItem -LiteralPath $temp -File | Remove-Item
 Remove-Item -LiteralPath $temp
}
