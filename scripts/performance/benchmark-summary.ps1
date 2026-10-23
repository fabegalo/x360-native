# Pure reduction of the runtime CSV. No game launch or machine access.
function Get-BenchmarkSummary {
 param([string]$Csv,[double]$StartSeconds,[double]$DurationSeconds,[int]$LogicalProcessors)
 if([double]::IsNaN($StartSeconds) -or [double]::IsInfinity($StartSeconds) -or
    [double]::IsNaN($DurationSeconds) -or [double]::IsInfinity($DurationSeconds) -or
    $DurationSeconds -le 0 -or $StartSeconds -lt 0 -or $LogicalProcessors -lt 1){throw 'Invalid measurement window or processor count'}
 $csvText=[IO.File]::ReadAllText([IO.Path]::GetFullPath($Csv))
 $discardedTail=0
 # Native records end in a newline. Forced cleanup may interrupt a buffered
 # write; do not interpret the truncated numeric prefix as a new timestamp.
 if($csvText.Length -gt 0 -and !$csvText.EndsWith("`n")) {
  $lastNewline=$csvText.LastIndexOf("`n")
  if($lastNewline -lt 0){throw 'Incomplete CSV header'}
  $csvText=$csvText.Substring(0,$lastNewline+1);$discardedTail=1
 }
 $rows=@($csvText | ConvertFrom-Csv)
 if($rows.Count -lt 2){throw 'Insufficient runtime frames'}
 $frames=[Collections.Generic.List[double]]::new()
 $cpu=[Collections.Generic.List[double]]::new()
 $totals=[ordered]@{}
 $captureTotals=[ordered]@{}
 $counters=@('draw_calls','command_buffer_stalls','vertices_processed','functions_dispatched','interrupt_dispatches','texture_cache_hits','texture_cache_misses','pipeline_cache_hits','pipeline_cache_misses','shader_translation_us','shader_translations','pipeline_creation_us','pipeline_creations','swap_failures')
 foreach($name in $counters){
  $totals[$name]=[long]0
  $captureTotals[$name]=[long]0
 }
 # Validate the entire capture, including warmup and trailing complete rows.
 # Native timestamps/counters are nonnegative integers; casting missing values
 # to double would silently invent zero. CPU may be absent on unsupported hosts.
 $lastTime=[long]-1;$lastCpu=[long]-1
 foreach($row in $rows) {
  foreach($name in @('runtime_elapsed_us')+$counters) {
   $value=[long]0
   if([string]$row.$name -notmatch '^[0-9]+$' -or ![long]::TryParse([string]$row.$name,[ref]$value)) {throw "Invalid or missing $name"}
   if($name -eq 'runtime_elapsed_us') {
    if($value -le $lastTime){throw 'Non-monotonic frame timestamps'}
    $lastTime=$value
   } else {
    if($value -gt [long]::MaxValue-$captureTotals[$name]){throw "Counter overflow: $name"}
    $captureTotals[$name]+=$value
   }
  }
  if($null -ne $row.process_cpu_us -and $row.process_cpu_us -ne '') {
   $value=[long]0
   if([string]$row.process_cpu_us -notmatch '^[0-9]+$' -or ![long]::TryParse([string]$row.process_cpu_us,[ref]$value)){throw 'Invalid process CPU time'}
   if($value -lt $lastCpu){throw 'Non-monotonic process CPU time'}
   $lastCpu=$value
  }
 }
 for($i=1;$i -lt $rows.Count;$i++) {
  $r=$rows[$i];$prev=$rows[$i-1]
  $t=[double]$r.runtime_elapsed_us/1e6;$p=[double]$prev.runtime_elapsed_us/1e6
  if($p -lt $StartSeconds -or $t -gt ($StartSeconds+$DurationSeconds)){continue}
  $dt=($t-$p)*1000
  if($dt -le 0){throw 'Non-monotonic frame timestamps'}
  $frames.Add($dt)
  if($null -ne $r.process_cpu_us -and $null -ne $prev.process_cpu_us -and
     $r.process_cpu_us -ne '' -and $prev.process_cpu_us -ne '') {
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
 $unavailable=[ordered]@{gpu_ms_per_frame='No GPU timestamp queries wired';vram_bytes='No process VRAM provider wired';gameplay_ready_ms='No semantic ready signal; runtime-to-first-swap is separate';cpu_critical_path_ms='Process CPU is aggregate across all threads';hardware_cache_misses='No PMU sampling';pipeline_fast_path_hits='Existing counter excludes consecutive identical pipeline fast path'}
 if($null -eq $cpuMean){$unavailable['process_cpu_ms_per_frame']='Missing process CPU samples in measurement window';$unavailable['process_cpu_percent_machine']='Missing process CPU samples in measurement window'}
 [ordered]@{
  frame_source='completed guest XE_SWAP cadence, not display scanout or GPU duration'
  discarded_unterminated_records=$discardedTail
  frames=$frames.Count;measured_seconds=$sum/1000
  requested_seconds=$DurationSeconds
  window_coverage_fraction=$sum/(1000*$DurationSeconds)
  boundary_excluded_ms=[Math]::Max([double]0,1000*$DurationSeconds-$sum)
  window_complete=([double]$rows[0].runtime_elapsed_us/1e6 -le $StartSeconds -and
    [double]$rows[-1].runtime_elapsed_us/1e6 -ge $StartSeconds+$DurationSeconds -and
    $sum -ge .95*1000*$DurationSeconds)
  fps_mean=1000*$frames.Count/$sum;fps_1pct_low=1000/$slowMean
  frame_ms_mean=$sum/$frames.Count
  frame_ms_p50=$sorted[[Math]::Ceiling(.50*$sorted.Count)-1]
  frame_ms_p95=$sorted[[Math]::Ceiling(.95*$sorted.Count)-1]
  frame_ms_p99=$sorted[[Math]::Ceiling(.99*$sorted.Count)-1]
  process_cpu_ms_per_frame=$cpuMean;process_cpu_percent_machine=$cpuPercent
  runtime_to_first_swap_ms=[double]$rows[0].runtime_elapsed_us/1000
  gpu_ms_per_frame=$null;vram_bytes=$null;gameplay_ready_ms=$null
  unavailable=$unavailable
  counters=$totals
  whole_capture_counters=$captureTotals
 }
}
