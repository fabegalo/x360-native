#requires -Version 7.0
param([Parameter(Mandatory)][string[]]$Runs,[Parameter(Mandatory)][string]$Output)
$ErrorActionPreference='Stop'
$reasons=[Collections.Generic.List[string]]::new()
$data=@(foreach($run in $Runs){
 @{environment=Get-Content (Join-Path $run 'environment.json') -Raw | ConvertFrom-Json
   result=Get-Content (Join-Path $run 'result.json') -Raw | ConvertFrom-Json}
})
function Identity($e) {
 # All executables AND runtime DLLs must be identical: only perf switches vary.
 $identity=[ordered]@{}
 foreach($name in @('title','scenario','hardware','measurement_clock','lock_sha256','sdk_head')){$identity[$name]=$e.$name}
 foreach($name in @('artifacts','collector','seed','original_xex')) {
  $identity[$name]=@($e.$name | ForEach-Object {"$($_.file)$($_.path):$($_.sha256)"} | Sort-Object)
 }
 $identity['reduction_implementation']=if($e.reduction){@($e.reduction.script_sha256,$e.reduction.summary_sha256,$e.reduction.process_summary_sha256)}else{$null}
 $identity['arguments']=@($e.arguments | Where-Object {$_ -notmatch '^--(log_file|user_data_root|cache_root|perf_log_csv|perf_counters)='} | Sort-Object)
 $identity | ConvertTo-Json -Depth 20 -Compress
}
if($data.Count -notin @(4,8)){$reasons.Add('Expected 4 or 8 chronological ABBA/BAAB runs')}
$reference=Identity $data[0].environment
$expected=@('off','on','on','off','on','off','off','on')
function PositiveNumber($Value) {
 $number=0.0
 $null -ne $Value -and [double]::TryParse([string]$Value,[Globalization.NumberStyles]::Float,[Globalization.CultureInfo]::InvariantCulture,[ref]$number) -and [double]::IsFinite($number) -and $number -gt 0
}
for($i=0;$i -lt $data.Count;$i++) {
 $e=$data[$i].environment;$r=$data[$i].result
 if($e.instrumentation -ne $expected[$i] -or $r.instrumentation -ne $e.instrumentation){$reasons.Add('Incorrect interleaved instrumentation order')}
 if($i -gt 0 -and [DateTime]$e.utc -le [DateTime]$data[$i-1].environment.utc){$reasons.Add('Runs not in chronological order')}
 if((Identity $e) -ne $reference){$reasons.Add('Different scenario, hardware, binaries, seed, originals, collector or settings')}
 if(!$e.artifacts -or !@($e.artifacts | Where-Object {$_.file -like '*.exe' -and $_.sha256}).Count -or !$e.collector -or !$e.original_xex -or !$e.hardware.cpu -or !$e.measurement_clock){$reasons.Add('Missing provenance')}
 if(!$r.scene_verified -or !$e.scene_verified -or !$e.scene_review){$reasons.Add('Missing scene review')}
 if($e.failure -or $r.measurement_error -or $e.calibration_only -or $r.calibration_only){$reasons.Add('Failed or calibration-only run')}
 if(!$r.window_complete -or !$r.external_window_complete){$reasons.Add('Incomplete measurement window')}
 foreach($name in @('external_process_cpu_ms_per_second','external_measured_seconds','ram_working_set_peak_bytes','private_commit_peak_bytes')) {
  if(!(PositiveNumber $r.$name)){$reasons.Add("Missing or invalid measurement: $name")}
 }
 if($r.external_start_seconds -ne $e.scenario.start_seconds -or $r.external_measured_seconds -ne $e.scenario.duration_seconds){$reasons.Add('External window differs from scenario')}
 if($e.instrumentation -eq 'off') {
  if('--perf_counters=false' -notin $e.arguments -or @($e.arguments | Where-Object {$_ -match '^--perf_log_csv='}).Count){$reasons.Add('OFF contains enabled instrumentation')}
  if($null -ne $r.fps_mean -or $null -ne $r.counters){$reasons.Add('OFF fabricated instrumented metrics')}
 } else {
  if('--perf_counters=true' -notin $e.arguments -or !@($e.arguments | Where-Object {$_ -match '^--perf_log_csv=.+$'}).Count){$reasons.Add('ON missing complete instrumentation')}
  foreach($name in @('fps_mean','fps_1pct_low','frame_ms_mean','frame_ms_p95','frame_ms_p99')) {
   if(!(PositiveNumber $r.$name)){$reasons.Add("ON missing frame metric: $name")}
  }
  if($null -eq $r.counters.swap_failures -or $r.counters.swap_failures -ne 0){$reasons.Add('Missing or failed native swaps')}
 }
}
if($reasons.Count) {
 $rejected=@{schema=1;comparable=$false;reasons=@($reasons | Select-Object -Unique);metrics=$null}
 $rejected | ConvertTo-Json -Depth 10 | Set-Content $Output -Encoding utf8
 $rejected | ConvertTo-Json -Depth 10
 exit 2
}
function Statistics($Values) {
 $v=@($Values | Where-Object {$null -ne $_})
 if(!$v.Count){return $null}
 $mean=($v | Measure-Object -Average).Average
 $ss=0.0;foreach($x in $v){$ss+=($x-$mean)*($x-$mean)}
 @{n=$v.Count;mean=$mean;sd=if($v.Count -gt 1){[Math]::Sqrt($ss/($v.Count-1))}else{$null};min=($v|Measure-Object -Minimum).Minimum;max=($v|Measure-Object -Maximum).Maximum}
}
$metrics=[ordered]@{}
foreach($name in @('external_process_cpu_ms_per_second','external_process_cpu_percent_machine','ram_working_set_peak_bytes','private_commit_peak_bytes','fps_mean','fps_1pct_low','frame_ms_mean','frame_ms_p95','frame_ms_p99')) {
 $off=Statistics @($data | Where-Object {$_.environment.instrumentation -eq 'off'} | ForEach-Object {$_.result.$name})
 $on=Statistics @($data | Where-Object {$_.environment.instrumentation -eq 'on'} | ForEach-Object {$_.result.$name})
 $metrics[$name]=@{off=$off;on=$on;change_percent=if($off -and $on -and $off.mean -ne 0){100*($on.mean/$off.mean-1)}else{$null}}
}
$paired=@(for($i=0;$i+1 -lt $data.Count;$i+=2) {
 $a=$data[$i];$b=$data[$i+1]
 if($a.environment.instrumentation -eq 'on'){$swap=$a;$a=$b;$b=$swap}
 $off=$a.result.external_process_cpu_ms_per_second;$on=$b.result.external_process_cpu_ms_per_second
 @{off_label=$a.environment.label;on_label=$b.environment.label;delta_cpu_ms_per_second=$on-$off;change_percent=if($off -gt 0){100*($on/$off-1)}else{$null}}
})
$difference=Statistics @($paired | ForEach-Object {$_.delta_cpu_ms_per_second})
$ci=$null
if($difference.n -in @(2,4)) {
 $critical=if($difference.n -eq 2){12.706204736}else{3.182446305}
 $margin=$critical*$difference.sd/[Math]::Sqrt($difference.n)
 $ci=@{low=$difference.mean-$margin;high=$difference.mean+$margin;confidence=.95;method='Paired Student t, adjacent runs; exploratory normal-independent-pair assumption, not a frame-level CI'}
}
$report=[ordered]@{
 schema=1;comparable=($reasons.Count -eq 0);reasons=@($reasons | Select-Object -Unique)
 title=$data[0].environment.title;labels=@($data | ForEach-Object {$_.environment.label})
 order=@($data | ForEach-Object {$_.environment.instrumentation});metrics=$metrics
 paired_cpu_differences=$paired;cpu_difference_ms_per_second=$difference;cpu_difference_ci95=$ci
 interpretation=if($reasons.Count){'Rejected: review reasons before interpreting deltas'}elseif($ci.low -gt 0){'Positive process CPU difference in this sample; limited repetitions and nondeterministic scenes restrict causal attribution'}elseif($ci.high -lt 0){'Negative CPU difference: investigate variability; do not infer negative instrumentation cost'}else{'CPU difference is unresolved within observed run-to-run variation'}
 scope='Observed total process CPU per wall second with counters, snapshots, native CPU timestamps and buffered CSV ON versus runtime OFF, under the same external sampler. Not compile-time removal of disabled callsites. Without independent OFF frames this does not establish equal throughput or cost per frame; FPS overhead remains unavailable.'
}
$report | ConvertTo-Json -Depth 20 | Set-Content $Output -Encoding utf8
$report | ConvertTo-Json -Depth 20
if($reasons.Count){exit 2}
