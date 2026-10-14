# Compare only reviewed complete runs. Reports deltas, not statistical proof.
param(
 [Parameter(Mandatory)][string]$Before,
 [Parameter(Mandatory)][string]$After,
 [string[]]$ChangedArtifacts=@('rexruntimerd.dll','rexruntime.dll'),
 [string]$Output
)
$ErrorActionPreference='Stop'
$a=Get-Content (Join-Path $Before 'result.json') -Raw | ConvertFrom-Json
$b=Get-Content (Join-Path $After 'result.json') -Raw | ConvertFrom-Json
$ea=Get-Content (Join-Path $Before 'environment.json') -Raw | ConvertFrom-Json
$eb=Get-Content (Join-Path $After 'environment.json') -Raw | ConvertFrom-Json
$reasons=[Collections.Generic.List[string]]::new()
if(!$a.window_complete -or !$b.window_complete){$reasons.Add('Incomplete measurement window')}
if(!$a.scene_verified -or !$b.scene_verified){$reasons.Add('Scene captures not reviewed')}
if($ea.title -ne $eb.title -or $ea.scenario_sha256 -ne $eb.scenario_sha256){$reasons.Add('Different title/scenario')}
if(($ea.hardware | ConvertTo-Json -Depth 10 -Compress) -ne ($eb.hardware | ConvertTo-Json -Depth 10 -Compress)){$reasons.Add('Different hardware/configuration')}
$seeda=@($ea.seed | ForEach-Object {"$($_.path):$($_.sha256)"} | Sort-Object)
$seedb=@($eb.seed | ForEach-Object {"$($_.path):$($_.sha256)"} | Sort-Object)
if(($seeda -join "`n") -ne ($seedb -join "`n")){$reasons.Add('Different user/cache seeds')}
$artifactsA=@($ea.artifacts | Where-Object {$_.file -notin $ChangedArtifacts} | ForEach-Object {"$($_.file):$($_.sha256)"} | Sort-Object)
$artifactsB=@($eb.artifacts | Where-Object {$_.file -notin $ChangedArtifacts} | ForEach-Object {"$($_.file):$($_.sha256)"} | Sort-Object)
if(($artifactsA -join "`n") -ne ($artifactsB -join "`n")){$reasons.Add('Unapproved binary/configuration changes')}
$delta=[ordered]@{}
foreach($name in @('fps_mean','fps_1pct_low','frame_ms_mean','frame_ms_p95','frame_ms_p99','process_cpu_ms_per_frame','runtime_to_first_swap_ms','ram_working_set_peak_bytes')) {
 $pct=$null
 if($null -ne $a.$name -and $null -ne $b.$name -and $a.$name -ne 0){$pct=100*($b.$name/$a.$name-1)}
 $delta[$name]=@{before=$a.$name;after=$b.$name;change_percent=$pct}
}
$r=[ordered]@{schema=1;comparable=($reasons.Count -eq 0);reasons=@($reasons);allowed_changed_artifacts=$ChangedArtifacts;metrics=$delta;before_counters=$a.counters;after_counters=$b.counters;before_log_counts=$a.log_counts;after_log_counts=$b.log_counts;note='Single-run delta; repeat alternating A/B and report dispersion before claiming a game-speed gain'}
$json=$r | ConvertTo-Json -Depth 12
if($Output){$json | Set-Content -LiteralPath $Output -Encoding utf8}
$json
if($reasons.Count){exit 2}
