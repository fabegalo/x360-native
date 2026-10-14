#requires -Version 7.0
# Bounded real-title diagnostic. Uses original hash guards, isolated copies of
# user/cache state, current native artifacts; does not publish preparation receipts.
param(
 [Parameter(Mandatory)][ValidateSet('dash_17559','castle_crashers','forza_horizon')][string]$Title,
 [Parameter(Mandatory)][ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$Label,
 [string]$ScenarioFile,
 [string]$BinaryDirectory,
 [switch]$NoCapture
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. "$PSScriptRoot/benchmark-summary.ps1"
. "$PSScriptRoot/benchmark-window.ps1"
function Save-BoundedCapture([int]$ProcessId,[string]$Path) {
 # PrintWindow may block on an unresponsive title. Keep it in an owned helper
 # so a capture cannot defeat the title's bounded execution contract.
 $captureInfo=[Diagnostics.ProcessStartInfo]::new()
 $captureInfo.FileName=(Get-Process -Id $PID).Path
 $captureInfo.UseShellExecute=$false;$captureInfo.CreateNoWindow=$true
 $captureInfo.RedirectStandardOutput=$true;$captureInfo.RedirectStandardError=$true
 foreach($arg in @('-NoProfile','-File',"$PSScriptRoot/benchmark-window.ps1",'-CaptureProcessId',"$ProcessId",'-CapturePath',$Path)){$captureInfo.ArgumentList.Add($arg)}
 $helper=[Diagnostics.Process]::Start($captureInfo)
 $helperOut=$helper.StandardOutput.ReadToEndAsync();$helperErr=$helper.StandardError.ReadToEndAsync()
 try {
  if(!$helper.WaitForExit(5000)){throw 'Window capture exceeded 5 seconds'}
  if($helper.ExitCode -ne 0 -or !(Test-Path $Path)){throw "Window capture failed: $($helperErr.Result)"}
 } finally {
  if(!$helper.HasExited){$helper.Kill($true);[void]$helper.WaitForExit(1000)}
  $helper.Dispose()
 }
}
if(!$ScenarioFile){$ScenarioFile=Join-Path $root 'benchmarks/scenarios.json'}
$scenario=(Get-Content $ScenarioFile -Raw | ConvertFrom-Json).scenarios.$Title
if(!$scenario){throw 'Scenario missing'}
$start=[double]$scenario.start_seconds;$duration=[double]$scenario.duration_seconds
if(![double]::IsFinite($start) -or ![double]::IsFinite($duration) -or $start -lt 5 -or $duration -lt 5 -or $start+$duration -gt 280){throw 'Scenario plus cleanup must finish within 300 seconds'}
if(!$NoCapture -and $start -lt 8){throw 'Window capture requires at least 8 seconds of warmup'}
foreach($event in $scenario.events){
 if(![double]::IsFinite([double]$event.seconds) -or ![double]::IsFinite([double]$event.hold_seconds)){throw 'Input times must be finite'}
 if([double]$event.hold_seconds -le 0 -or [double]$event.hold_seconds -gt 3){throw 'Input hold must be within (0, 3] seconds'}
 if([double]$event.seconds -lt 0 -or [double]$event.seconds+[double]$event.hold_seconds -ge $start+$duration){throw 'Input event falls outside the bounded scenario'}
 if([int]$event.vk -le 0 -or [int]$event.vk -gt 255 -or [int]$event.scan -le 0 -or [int]$event.scan -gt 127){throw 'Invalid input key/scan code'}
}
$out=Join-Path $root ".local/benchmarks/$Label/$Title"
if(Test-Path $out){throw "Refusing to overwrite $out"}
New-Item -ItemType Directory -Force $out | Out-Null
if(!$BinaryDirectory){$BinaryDirectory=Join-Path $root ".local/build/titles/$Title"}
$exe=Join-Path ([IO.Path]::GetFullPath($BinaryDirectory)) "$Title.exe"
if(!(Test-Path $exe)){throw "Missing $exe"}
if(Get-Process -Name dash_17559,castle_crashers,forza_horizon -ErrorAction SilentlyContinue){throw 'A reference title is already running; benchmark requires exclusive session'}
function Inventory([string]$Directory) {
 @(Get-ChildItem -LiteralPath $Directory -File -Recurse | Sort-Object FullName | ForEach-Object {
  @{path=$_.FullName.Substring($Directory.Length).TrimStart('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLower()}
 })
}
# Immutable seed reused for both sides; never change real profiles, saves or caches.
$seed=Join-Path $root ".local/benchmarks/seeds/$Title"
if(!(Test-Path $seed)) {
 New-Item -ItemType Directory -Force $seed | Out-Null
 Get-ChildItem -LiteralPath (Join-Path $root $scenario.user_root) | Copy-Item -Destination $seed -Recurse
}
$seedInventory=Inventory $seed
$user=Join-Path $out 'user';New-Item -ItemType Directory $user | Out-Null
Get-ChildItem -LiteralPath $seed | Copy-Item -Destination $user -Recurse
$hardware=[ordered]@{logical_processors=[Environment]::ProcessorCount;os=[Environment]::OSVersion.VersionString;cpu=$null;ram_bytes=$null;gpu=$null;power_plan=$null}
try {
 $hardware.cpu=@(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors)
 $hardware.ram_bytes=(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory
 $hardware.gpu=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,CurrentHorizontalResolution,CurrentVerticalResolution,CurrentRefreshRate)
 $hardware.power_plan=(& powercfg /getactivescheme) -join ' '
}catch{$hardware['query_error']=$_.Exception.Message}
$artifacts=@(Get-ChildItem -LiteralPath (Split-Path $exe) -File | Where-Object {$_.Extension -in @('.exe','.dll','.toml')} | ForEach-Object {@{file=$_.Name;sha256=(Get-FileHash $_.FullName).Hash.ToLower()}})
$csv=Join-Path $out 'frames.csv'
$argsList=@('--gpu_plugin=xenos','--mnk_mode=true','--log_level=info',"--log_file=$out/rex.log","--game_data_root=$(Join-Path $root $scenario.game_root)","--user_data_root=$user","--cache_root=$user/cache","--perf_log_csv=$csv")+@($scenario.arguments)
$metadata=[ordered]@{schema=1;title=$Title;label=$Label;utc=[DateTime]::UtcNow.ToString('o');head=(& git -C $root rev-parse HEAD).Trim();sdk_head=(& git -C "$root/external/rexglue-sdk" rev-parse HEAD).Trim();lock_sha256=(Get-FileHash "$root/deps.lock.json").Hash.ToLower();scenario=$scenario;scenario_sha256=(Get-FileHash $ScenarioFile).Hash.ToLower();hardware=$hardware;artifacts=$artifacts;arguments=$argsList;seed=$seedInventory;cache_condition='identical existing cache seed, OS filesystem cache uncontrolled';termination=$null;scene_verified=$false;runtime_origin_approx_ms=$null}
$metadata | ConvertTo-Json -Depth 15 | Set-Content "$out/environment.json" -Encoding utf8
$metadata['collector']=@('benchmark-runtime.ps1','benchmark-window.ps1','benchmark-summary.ps1' | ForEach-Object {
 @{file=$_;sha256=(Get-FileHash (Join-Path $PSScriptRoot $_)).Hash.ToLower()}
})
$metadata['failure']=$null
$gameRoot=Join-Path $root $scenario.game_root
$metadata['original_xex']=@(Get-ChildItem -LiteralPath $gameRoot -Filter '*.xex' -File -Recurse | Sort-Object FullName | ForEach-Object {
 @{path=$_.FullName.Substring($gameRoot.Length).TrimStart('\','/');sha256=(Get-FileHash $_.FullName).Hash.ToLower()}
})
if(Test-Path (Join-Path $BinaryDirectory 'provenance.json')) {
 $metadata['binary_provenance']=Get-Content (Join-Path $BinaryDirectory 'provenance.json') -Raw | ConvertFrom-Json
}
$si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$exe;$si.WorkingDirectory=$out
$si.UseShellExecute=$false;$si.CreateNoWindow=$true
$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true
foreach($arg in $argsList){$si.ArgumentList.Add($arg)}
$samples=[Collections.Generic.List[object]]::new();$events=[Collections.Generic.List[object]]::new()
$pressId=0
foreach($event in $scenario.events){
 $pressId++
 $events.Add(@{at=[double]$event.seconds;vk=[int]$event.vk;scan=[int]$event.scan;down=$true;sent=$false;press=$pressId;hold=[double]$event.hold_seconds})
 $events.Add(@{at=[double]$event.seconds+[double]$event.hold_seconds;vk=[int]$event.vk;scan=[int]$event.scan;down=$false;sent=$false;press=$pressId})
}
$p=[Diagnostics.Process]::Start($si);$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync()
$clock=[Diagnostics.Stopwatch]::StartNew()
$before=$false;$after=$false;$nextSample=0;$runtimeOffset=$null
# Only the external 1-Hz RAM sample alignment uses NTFS file creation time.
# All FPS/CPU reductions use native monotonic timestamps and native CPU totals.
$processStartUtc=$p.StartTime.ToUniversalTime()
$metadata['pid']=$p.Id
try {
 while(!$p.HasExited -and $clock.Elapsed.TotalSeconds -lt $start+$duration+20) {
  $t=$clock.Elapsed.TotalSeconds
  foreach($event in $events){if(!$event.sent -and $t -ge $event.at){
   Send-BenchmarkKey $p.Id $event.vk $event.scan $event.down
   $event.sent=$true;$event.actual_seconds=$clock.Elapsed.TotalSeconds
   # Preserve the requested hold after asynchronous foreground/focus delivery.
   if($event.down){foreach($release in $events){if(!$release.down -and $release.press -eq $event.press){$release.at=$event.actual_seconds+$event.hold}}}
  }}
  if(!$NoCapture -and !$before -and $t -ge $start-6){Save-BoundedCapture $p.Id "$out/before.png";$before=$true}
  if($t -ge $nextSample){
   if(Test-Path $csv){
    if($null -eq $runtimeOffset){$runtimeOffset=([IO.File]::GetCreationTimeUtc($csv)-$processStartUtc).TotalSeconds;$metadata.runtime_origin_approx_ms=1000*$runtimeOffset}
   }
   $p.Refresh();$samples.Add(@{seconds=$t;runtime_seconds=if($null -ne $runtimeOffset){$t-$runtimeOffset}else{$null};working_set_bytes=$p.WorkingSet64;private_bytes=$p.PrivateMemorySize64;cpu_ms=$p.TotalProcessorTime.TotalMilliseconds});$nextSample=$t+1
  }
  # The SDK FILE stream may deny concurrent reads. Read timestamps only after
  # cleanup; use its creation timestamp plus generous margin to bound capture.
  if($null -ne $runtimeOffset -and $t-$runtimeOffset -ge $start+$duration+5){
   if(!$NoCapture){Save-BoundedCapture $p.Id "$out/after.png";$after=$true}
   break
  }
  Start-Sleep -Milliseconds 40
 }
 if($p.HasExited){$metadata.termination="early_exit:$($p.ExitCode)"}else{$metadata.termination='bounded_forced_stop';$p.Kill($true);[void]$p.WaitForExit(5000)}
}catch{
 $metadata.termination='runner_error'
 $metadata.failure=$_.Exception.Message
}finally{
 if(!$p.HasExited){$p.Kill($true);[void]$p.WaitForExit(5000)}
 $stdout.Result | Set-Content "$out/stdout.log";$stderr.Result | Set-Content "$out/stderr.log"
 $samples | ConvertTo-Json -Depth 5 | Set-Content "$out/process-samples.json"
 $events | ConvertTo-Json -Depth 5 | Set-Content "$out/input-events.json"
 $metadata | ConvertTo-Json -Depth 15 | Set-Content "$out/environment.json" -Encoding utf8
}
try {
 if($metadata.failure){throw $metadata.failure}
 $summary=Get-BenchmarkSummary $csv $start $duration $hardware.logical_processors
}catch{
 @{schema=1;title=$Title;label=$Label;window_complete=$false;scene_verified=$false;measurement_error=$_.Exception.Message;termination=$metadata.termination} |
  ConvertTo-Json | Set-Content "$out/result.json"
 throw
}
$selected=@($samples | Where-Object {$null -ne $_.runtime_seconds -and $_.runtime_seconds -ge $start -and $_.runtime_seconds -le $start+$duration})
$summary['ram_working_set_peak_bytes']=($selected.working_set_bytes | Measure-Object -Maximum).Maximum
$summary['private_commit_peak_bytes']=($selected.private_bytes | Measure-Object -Maximum).Maximum
$summary['title']=$Title;$summary['label']=$Label;$summary['termination']=$metadata.termination
$summary['scene_verified']=$false
$summary['log_counts']=@{error=0;warning=0;critical=0}
$summary['log_count_scope']='Retained rex*.log files only; rotation may discard earlier messages'
foreach($log in Get-ChildItem $out -Filter 'rex*.log') {
 foreach($level in @('error','warning','critical')) {$summary.log_counts[$level]+=@(Select-String -LiteralPath $log.FullName -Pattern "\[$level\]").Count}
}
$summary | ConvertTo-Json -Depth 15 | Set-Content "$out/result.json" -Encoding utf8
$summary | ConvertTo-Json -Depth 6
