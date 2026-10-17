# Independent of rex::perf: identical process CPU/RAM provider for ON and OFF.
function Get-BenchmarkProcessSummary($Samples,[double]$StartSeconds,[double]$DurationSeconds,[int]$LogicalProcessors) {
 if(![double]::IsFinite($StartSeconds) -or ![double]::IsFinite($DurationSeconds) -or $StartSeconds -lt 0 -or $DurationSeconds -le 0 -or $LogicalProcessors -lt 1){throw 'Invalid process measurement window'}
 $rows=@($Samples)
 if($rows.Count -lt 2){throw 'Missing process samples'}
 $previous=$null
 foreach($row in $rows) {
  foreach($key in @('seconds','cpu_ms','working_set_bytes','private_bytes')) {
   if($null -eq $row.$key -or ![double]::IsFinite([double]$row.$key) -or [double]$row.$key -lt 0){throw "Invalid process sample: $key"}
  }
  if($null -ne $previous -and ($row.seconds -le $previous.seconds -or $row.cpu_ms -lt $previous.cpu_ms)){throw 'Nonmonotonic process samples'}
  $previous=$row
 }
 $end=$StartSeconds+$DurationSeconds
 if($rows[0].seconds -gt $StartSeconds -or $rows[-1].seconds -lt $end){throw 'Process samples do not cover requested window'}
 $boundaryCpu=@()
 foreach($boundary in @($StartSeconds,$end)) {
  for($i=1;$i -lt $rows.Count;$i++) {
   if($rows[$i].seconds -ge $boundary) {
    $a=$rows[$i-1];$b=$rows[$i]
    $fraction=($boundary-$a.seconds)/($b.seconds-$a.seconds)
    $boundaryCpu+=($a.cpu_ms+$fraction*($b.cpu_ms-$a.cpu_ms));break
   }
  }
 }
 $gaps=@(for($i=1;$i -lt $rows.Count;$i++) {
  if($rows[$i].seconds -ge $StartSeconds -and $rows[$i-1].seconds -le $end){$rows[$i].seconds-$rows[$i-1].seconds}
 })
 $maxGap=($gaps | Measure-Object -Maximum).Maximum
 if($maxGap -gt 2.5){throw 'Process sample gap exceeds 2.5 seconds in measurement window'}
 $selected=@($rows | Where-Object {$_.seconds -ge $StartSeconds -and $_.seconds -le $end})
 if(!$selected.Count){throw 'No RAM samples in measurement window'}
 $cpu=$boundaryCpu[1]-$boundaryCpu[0]
 return [ordered]@{
  external_window_complete=$true;external_start_seconds=$StartSeconds;external_measured_seconds=$DurationSeconds
  external_process_cpu_ms=$cpu;external_process_cpu_ms_per_second=$cpu/$DurationSeconds
  external_process_cpu_percent_machine=$cpu/($DurationSeconds*10*$LogicalProcessors)
  external_max_sample_gap_seconds=$maxGap;external_sample_count=$selected.Count
  external_cpu_source='System.Diagnostics.Process.TotalProcessorTime; linear interpolation of cumulative CPU at window boundaries'
  ram_working_set_peak_bytes=($selected.working_set_bytes | Measure-Object -Maximum).Maximum
  private_commit_peak_bytes=($selected.private_bytes | Measure-Object -Maximum).Maximum
 }
}
