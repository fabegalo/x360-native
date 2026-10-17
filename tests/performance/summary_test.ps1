$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/performance/benchmark-summary.ps1"
$temp=Join-Path ([IO.Path]::GetTempPath()) ('x360n-bench-'+[guid]::NewGuid()+'.csv')
try {
 $fields=@('draw_calls','command_buffer_stalls','vertices_processed','functions_dispatched','interrupt_dispatches','texture_cache_hits','texture_cache_misses','pipeline_cache_hits','pipeline_cache_misses','shader_translation_us','shader_translations','pipeline_creation_us','pipeline_creations','swap_failures')
 $rows=0..100 | ForEach-Object {
  $row=[ordered]@{runtime_elapsed_us=$_*10000;process_cpu_us=$_*20000;frame_time_us=10000}
  foreach($name in $fields){$row[$name]=1}
  [pscustomobject]$row
 }
 $rows | Export-Csv -NoTypeInformation $temp
 $r=Get-BenchmarkSummary $temp 0 1 4
 if($r.frames -ne 100 -or [Math]::Abs($r.fps_mean-100) -gt .001 -or [Math]::Abs($r.fps_1pct_low-100) -gt .001){throw 'FPS aggregation failed'}
 if([Math]::Abs($r.frame_ms_p99-10) -gt .001 -or $r.process_cpu_ms_per_frame -ne 20 -or [Math]::Abs($r.process_cpu_percent_machine-50) -gt .001){throw 'CPU/percentile failed'}
 if(!$r.window_complete -or $r.counters.draw_calls -ne 100 -or $null -ne $r.gpu_ms_per_frame){throw 'Coverage/counter availability failed'}
 [IO.File]::AppendAllText($temp,'110')
 $r=Get-BenchmarkSummary $temp 0 1 4
 if(!$r.window_complete -or $r.discarded_unterminated_records -ne 1 -or $r.frames -ne 100){throw 'Partial final record corrupted completed window'}
 # A partial row inside the requested end must still fail coverage.
 $r=Get-BenchmarkSummary $temp 0 1.1 4
 if($r.window_complete){throw 'Discarded tail hid incomplete window'}
 $rows[100].runtime_elapsed_us=1100000
 $rows | Export-Csv -NoTypeInformation $temp
 $r=Get-BenchmarkSummary $temp 0 1.1 4
 if([Math]::Abs($r.fps_1pct_low-(1000/110)) -gt .001 -or [Math]::Abs($r.frame_ms_p99-10) -gt .001){throw 'Slowest 1%/nearest rank failed'}
 $r=Get-BenchmarkSummary $temp 0 2 4
 if($r.window_complete){throw 'Truncation hidden'}
 $rows[50].process_cpu_us='';$rows | Export-Csv -NoTypeInformation $temp
 $r=Get-BenchmarkSummary $temp 0 2 4
 if($null -ne $r.process_cpu_ms_per_frame){throw 'Missing CPU fabricated'}
 $rows | Select-Object -ExcludeProperty process_cpu_us -Property * | Export-Csv -NoTypeInformation $temp
 $r=Get-BenchmarkSummary $temp 0 2 4
 if($null -ne $r.process_cpu_ms_per_frame -or !$r.unavailable.process_cpu_ms_per_frame){throw 'Absent CPU provider fabricated'}
 foreach($row in $rows){$row.runtime_elapsed_us+=200000}
 $rows | Export-Csv -NoTypeInformation $temp
 $r=Get-BenchmarkSummary $temp 0 1 4
 if($r.window_complete -or $r.boundary_excluded_ms -lt 199){throw 'Late-starting CSV accepted as a full window'}
 # A long interval crossing warmup must not silently remove most of a hitch.
 $rows[0].runtime_elapsed_us=0
 $rows | Export-Csv -NoTypeInformation $temp
 $r=Get-BenchmarkSummary $temp .05 1 4
 if($r.window_complete -or $r.window_coverage_fraction -ge .95){throw 'Boundary stall hidden'}
 # Validation covers warmup and trailing rows, not only accepted intervals.
 $valid=@($rows | ConvertTo-Csv -NoTypeInformation)
 foreach($case in @('missing-time','warmup-backwards','tail-backwards','missing-counter','negative-counter','nan-cpu','cpu-backwards')) {
  $broken=@($valid | ConvertFrom-Csv)
  switch($case) {
   'missing-time' {$broken[0].runtime_elapsed_us=''}
   'warmup-backwards' {$broken[1].runtime_elapsed_us=0}
   'tail-backwards' {$broken[-1].runtime_elapsed_us=1}
   'missing-counter' {$broken[0].draw_calls=''}
   'negative-counter' {$broken[-1].draw_calls=-1}
   'nan-cpu' {$broken[0].process_cpu_us='NaN'}
   'cpu-backwards' {$broken[-1].process_cpu_us=0}
  }
  $broken | Export-Csv -NoTypeInformation $temp
  $rejected=$false
  try {Get-BenchmarkSummary $temp .3 .5 4 | Out-Null}catch{$rejected=$true}
  if(!$rejected){throw "Malformed capture accepted: $case"}
 }
 $valid | Set-Content $temp
 $rejected=$false
 try {Get-BenchmarkSummary $temp ([double]::NaN) 1 4 | Out-Null}catch{$rejected=$true}
 if(!$rejected){throw 'Nonfinite window accepted'}
 # Excluded duration must retain sub-millisecond precision.
 $fractional=0..100 | ForEach-Object {
  $row=[ordered]@{runtime_elapsed_us=$_*10000+250;process_cpu_us=$_*20000}
  foreach($name in $fields){$row[$name]=1};[pscustomobject]$row
 }
 $fractional | Export-Csv -NoTypeInformation $temp
 $r=Get-BenchmarkSummary $temp 0 1.0005 4
 if([Math]::Abs($r.boundary_excluded_ms-.5) -gt .00001){throw 'Excluded duration rounded to integer'}
 $fractional[0].draw_calls=[long]9007199254740993
 $fractional | Export-Csv -NoTypeInformation $temp
 $r=Get-BenchmarkSummary $temp 0 1.0005 4
 if($r.whole_capture_counters.draw_calls -ne [long]9007199254741093){throw 'Integer counter totals lost precision'}
 'Benchmark reduction: FPS, tail, CPU, missing data and incomplete window passed'
} finally {Remove-Item -LiteralPath $temp -ErrorAction SilentlyContinue}
