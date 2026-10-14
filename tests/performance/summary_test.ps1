$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/benchmark-summary.ps1"
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
 $rows[100].runtime_elapsed_us=1100000
 $rows | Export-Csv -NoTypeInformation $temp
 $r=Get-BenchmarkSummary $temp 0 1.1 4
 if([Math]::Abs($r.fps_1pct_low-(1000/110)) -gt .001 -or [Math]::Abs($r.frame_ms_p99-10) -gt .001){throw 'Slowest 1%/nearest rank failed'}
 $r=Get-BenchmarkSummary $temp 0 2 4
 if($r.window_complete){throw 'Truncation hidden'}
 $rows[50].process_cpu_us='';$rows | Export-Csv -NoTypeInformation $temp
 $r=Get-BenchmarkSummary $temp 0 2 4
 if($null -ne $r.process_cpu_ms_per_frame){throw 'Missing CPU fabricated'}
 'Benchmark reduction: FPS, tail, CPU, missing data and incomplete window passed'
} finally {Remove-Item -LiteralPath $temp -ErrorAction SilentlyContinue}
