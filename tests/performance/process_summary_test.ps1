#requires -Version 7.0
$ErrorActionPreference='Stop'
. "$PSScriptRoot/../../scripts/performance/benchmark-process-summary.ps1"
$rows=@(0..40 | ForEach-Object {@{seconds=[double]$_;cpu_ms=[double]$_*2000;working_set_bytes=1000+$_;private_bytes=2000+$_}})
$r=Get-BenchmarkProcessSummary $rows 5.25 30 16
if($r.external_process_cpu_ms -ne 60000 -or $r.external_process_cpu_percent_machine -ne 12.5 -or $r.external_sample_count -ne 30 -or $r.ram_working_set_peak_bytes -ne 1035){throw 'Process boundary interpolation/normalization failed'}
foreach($case in @('missing','negative','nan','backwards','cpu-backwards','gap','coverage')) {
 $bad=@($rows | ConvertTo-Json | ConvertFrom-Json)
 switch($case) {
  'missing' {$bad[10].cpu_ms=$null}
  'negative' {$bad[10].private_bytes=-1}
  'nan' {$bad[10].seconds=[double]::NaN}
  'backwards' {$bad[10].seconds=9}
  'cpu-backwards' {$bad[10].cpu_ms=0}
  'gap' {$bad=@($bad | Where-Object {$_.seconds -lt 10 -or $_.seconds -gt 13})}
  'coverage' {$bad=@($bad | Where-Object {$_.seconds -lt 35})}
 }
 $rejected=$false
 try{Get-BenchmarkProcessSummary $bad 5.25 30 16 | Out-Null}catch{$rejected=$true}
 if(!$rejected){throw "Invalid process evidence accepted: $case"}
}
'Process summary: interpolation, CPU normalization, sampled RAM and 7 rejection cases passed'
