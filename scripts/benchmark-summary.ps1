# Pure reduction of the runtime CSV. No game launch or machine access.
function Get-BenchmarkSummary {
 param([string]$Csv,[double]$StartSeconds,[double]$DurationSeconds,[int]$LogicalProcessors)
 $rows=@(Import-Csv -LiteralPath $Csv)
 if($rows.Count -lt 2){throw 'Insufficient runtime frames'}
 $frames=[Collections.Generic.List[double]]::new()
 $cpu=[Collections.Generic.List[double]]::new()
 $totals=[ordered]@{}
 $captureTotals=[ordered]@{}
 $counters=@('draw_calls','command_buffer_stalls','vertices_processed','functions_dispatched','interrupt_dispatches','texture_cache_hits','texture_cache_misses','pipeline_cache_hits','pipeline_cache_misses','shader_translation_us','shader_translations','pipeline_creation_us','pipeline_creations','swap_failures')
 foreach($name in $counters){
  $totals[$name]=[long]0
  $captureTotals[$name]=[long](($rows | Measure-Object -Property $name -Sum).Sum)
 }
 for($i=1;$i -lt $rows.Count;$i++) {
  $r=$rows[$i];$prev=$rows[$i-1]
  $t=[double]$r.runtime_elapsed_us/1e6;$p=[double]$prev.runtime_elapsed_us/1e6
  if($p -lt $StartSeconds -or $t -gt ($StartSeconds+$DurationSeconds)){continue}
  $dt=($t-$p)*1000
  if($dt -le 0){throw 'Non-monotonic frame timestamps'}
  $frames.Add($dt)
  if($r.process_cpu_us -ne '' -and $prev.process_cpu_us -ne '') {
   $delta=([double]$r.process_cpu_us-[double]$prev.process_cpu_us)/1000
   if($delta -lt 0){throw 'Non-monotonic process CPU time'}
   $cpu.Add($delta)
  }
  foreach($name in $counters){
   if($null -eq $r.$name -or $r.$name -eq ''){throw "Missing counter $name"}
   $totals[$name]+=[long]$r.$name
  }
 }
 if($frames.Count -lt 2){throw 'No usable measurement window'}
 $sorted=@($frames | Sort-Object)
 $sum=($frames | Measure-Object -Sum).Sum
 $slow=[Math]::Max(1,[int][Math]::Ceiling($frames.Count*.01))
 $slowMean=($sorted[($sorted.Count-$slow)..($sorted.Count-1)] | Measure-Object -Average).Average
 $cpuMean=$null;$cpuPercent=$null
 if($cpu.Count -eq $frames.Count){
  $cpuSum=($cpu | Measure-Object -Sum).Sum
  $cpuMean=$cpuSum/$frames.Count
  $cpuPercent=100*$cpuSum/$sum/$LogicalProcessors
 }
 [ordered]@{
  frame_source='completed guest XE_SWAP cadence, not display scanout or GPU duration'
  frames=$frames.Count;measured_seconds=$sum/1000
  window_complete=([double]$rows[-1].runtime_elapsed_us/1e6 -ge $StartSeconds+$DurationSeconds)
  fps_mean=1000*$frames.Count/$sum;fps_1pct_low=1000/$slowMean
  frame_ms_mean=$sum/$frames.Count
  frame_ms_p95=$sorted[[Math]::Ceiling(.95*$sorted.Count)-1]
  frame_ms_p99=$sorted[[Math]::Ceiling(.99*$sorted.Count)-1]
  process_cpu_ms_per_frame=$cpuMean;process_cpu_percent_machine=$cpuPercent
  runtime_to_first_swap_ms=[double]$rows[0].runtime_elapsed_us/1000
  gpu_ms_per_frame=$null;vram_bytes=$null;gameplay_ready_ms=$null
  unavailable=[ordered]@{gpu_ms_per_frame='No GPU timestamp queries wired';vram_bytes='No process VRAM provider wired';gameplay_ready_ms='No semantic ready signal; runtime-to-first-swap is separate';cpu_critical_path_ms='Process CPU is aggregate across all threads';hardware_cache_misses='No PMU sampling';pipeline_fast_path_hits='Existing counter excludes consecutive identical pipeline fast path'}
  counters=$totals
  whole_capture_counters=$captureTotals
 }
}
