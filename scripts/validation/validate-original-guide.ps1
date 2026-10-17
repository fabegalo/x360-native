#requires -Version 5.1
# Explicit diagnostic only. Uses original guest UI and PID-scoped local input;
# captures are evidence to review, never an automatic visual acceptance verdict.
param(
 [Parameter(Mandatory)][string]$Executable,
 [Parameter(Mandatory)][string]$GameRoot,
 [Parameter(Mandatory)][string]$SeedUserData,
 [Parameter(Mandatory)][string]$ScenarioFile,
 [Parameter(Mandatory)][string]$OutputDirectory,
 [string]$SystemUiHost,
 [string]$SystemUiRoot
)
$ErrorActionPreference='Stop'
$root= (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. "$PSScriptRoot/../performance/benchmark-window.ps1"
# Windows PowerShell 5.1 has no ProcessStartInfo.ArgumentList: quote each
# argument with the CommandLineToArgvW rules instead.
function Join-ProcessArguments([string[]]$Values){
 ($Values | ForEach-Object {
  if($_ -notmatch '[\s"]' -and $_){$_}else{'"'+(($_ -replace '(\\*)"','$1$1\"') -replace '(\\+)$','$1$1')+'"'}
 }) -join ' '
}
function Stop-ProcessTree([Diagnostics.Process]$Process){
 & taskkill.exe /PID $Process.Id /T /F 2>&1 | Out-Null
}
$exe=(Resolve-Path -LiteralPath $Executable).Path
$game=(Resolve-Path -LiteralPath $GameRoot).Path
$seed=(Resolve-Path -LiteralPath $SeedUserData).Path
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(!$exe.StartsWith((Join-Path $root '.local\build\'),[StringComparison]::OrdinalIgnoreCase)) {throw 'Use an existing local build executable'}
if(!$out.StartsWith((Join-Path $root '.local\validation\'),[StringComparison]::OrdinalIgnoreCase)) {throw 'Evidence must be under .local/validation'}
if(Test-Path -LiteralPath $out){throw 'Use a new evidence directory'}
if($out.StartsWith($seed.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Evidence must be outside the seed directory'}
if([bool]$SystemUiHost -ne [bool]$SystemUiRoot){throw 'System UI host and original root must be supplied together'}
$plan=Get-Content -LiteralPath $ScenarioFile -Raw | ConvertFrom-Json
$seconds=[double]$plan.seconds
if([double]::IsNaN($seconds) -or [double]::IsInfinity($seconds) -or $seconds -lt 5 -or $seconds -gt 300){throw 'Scenario duration must be 5..300 seconds'}
foreach($arg in $plan.arguments){
 if($arg -ne '--dash17559_boot_intro=false'){throw 'Scenario arguments may only disable the dashboard intro'}
}
$previous=-1.0
foreach($event in $plan.events){
 $at=[double]$event.at
 if([double]::IsNaN($at) -or [double]::IsInfinity($at) -or $at -lt 0 -or $at -lt $previous -or $at -ge $seconds){throw 'Events must be ordered within the scenario duration'}
 $previous=$at
 if($event.action -eq 'shot'){
  if($event.name -notmatch '^[a-z0-9-]+$'){throw 'Invalid screenshot name'}
 }elseif($event.action -eq 'key'){
  if($event.vk -lt 1 -or $event.vk -gt 255 -or $event.scan -lt 1 -or $event.scan -gt 127 -or
     $event.hold_ms -lt 30 -or $event.hold_ms -gt 3000 -or $at+$event.hold_ms/1000.0 -ge $seconds){throw 'Invalid key event'}
 }else{throw 'Only key and shot events are supported'}
}
if(@($plan.events | Where-Object action -eq 'shot' | Group-Object name | Where-Object Count -gt 1).Count){throw 'Screenshot names must be unique'}
if(Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($exe)) -ErrorAction SilentlyContinue){throw 'The selected title is already running'}
New-Item -ItemType Directory -Path $out | Out-Null
Copy-Item -LiteralPath $ScenarioFile -Destination "$out/scenario.json"
New-Item -ItemType Directory -Path "$out/user" | Out-Null
Get-ChildItem -LiteralPath $seed -Force | Copy-Item -Destination "$out/user" -Recurse
function HashFiles([string]$Directory,[string[]]$Extensions){
 @(Get-ChildItem -LiteralPath $Directory -File | Where-Object {$_.Extension -in $Extensions} | ForEach-Object {
  @{path=$_.FullName;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()}
 })
}
$metadata=[ordered]@{
 diagnostic=$true;visual_acceptance=$false;head=(& git -C $root rev-parse HEAD).Trim()
 lock_sha256=(Get-FileHash "$root/deps.lock.json").Hash.ToLowerInvariant()
 executable=$exe;game_root=$game;seed=$seed;seconds=$seconds
 binaries=(HashFiles (Split-Path $exe) @('.exe','.dll'));system_ui_binaries=@()
}
$si=[Diagnostics.ProcessStartInfo]::new()
$si.FileName=$exe;$si.WorkingDirectory=$out;$si.UseShellExecute=$false;$si.CreateNoWindow=$true
$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true
$arguments=@('--gpu_plugin=xenos','--mnk_mode=true','--log_level=info',
 '--keybind_guide=G','--keybind_dpad_up=U','--keybind_dpad_down=J','--keybind_dpad_left=N','--keybind_dpad_right=H',
 "--game_data_root=$game","--user_data_root=$out/user","--cache_root=$out/user/cache",
 "--log_file=$out/rex.log","--x360n_handoff_out=$out/handoff.bin",
 # The same account store a supervisor session passes to every process.
 "--x360n_profile_root=$out/user")+@($plan.arguments | Where-Object {$_})
if($SystemUiHost){
 $hostPath=(Resolve-Path -LiteralPath $SystemUiHost).Path
 $systemRoot=(Resolve-Path -LiteralPath $SystemUiRoot).Path
 $arguments+=@("--x360n_system_ui_host=$hostPath","--x360n_system_ui_root=$systemRoot")
 $metadata.system_ui_binaries=HashFiles (Split-Path $hostPath) @('.dll')
}
$metadata['arguments']=$arguments
$metadata | ConvertTo-Json -Depth 8 | Set-Content "$out/environment.json"
$si.Arguments=Join-ProcessArguments $arguments
$p=$null;$clock=[Diagnostics.Stopwatch]::new();$failure=$null;$completed=0;$forced=$false
try {
 $p=[Diagnostics.Process]::Start($si)
 $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync();$clock.Start()
 foreach($event in $plan.events){
  while($clock.Elapsed.TotalSeconds -lt $event.at){
   if($p.WaitForExit(20)){throw 'Title exited before the next scheduled event'}
  }
  if($clock.Elapsed.TotalSeconds -ge $seconds){throw 'Scenario deadline exceeded'}
  if($event.action -eq 'key'){
   Send-BenchmarkKey $p.Id $event.vk $event.scan $true
   try {[void]$p.WaitForExit($event.hold_ms)}finally{if(!$p.HasExited){Send-BenchmarkKey $p.Id $event.vk $event.scan $false}}
  }else{
   # PrintWindow can block. An owned helper has a separate five-second timeout.
   $capture=[Diagnostics.ProcessStartInfo]::new()
   $capture.FileName=(Get-Process -Id $PID).Path;$capture.UseShellExecute=$false;$capture.CreateNoWindow=$true
   $capture.Arguments=Join-ProcessArguments @('-NoProfile','-ExecutionPolicy','Bypass','-File',"$PSScriptRoot/../performance/benchmark-window.ps1",'-CaptureProcessId',"$($p.Id)",'-CapturePath',"$out/$($event.name).png")
   $helper=[Diagnostics.Process]::Start($capture)
   try {
    if(!$helper.WaitForExit(5000)){throw 'Capture timeout'}
    if($helper.ExitCode -ne 0){throw 'Capture failed'}
   }finally{if(!$helper.HasExited){Stop-ProcessTree $helper;[void]$helper.WaitForExit(1000)};$helper.Dispose()}
  }
  $completed++
  @{event=$event;actual_seconds=$clock.Elapsed.TotalSeconds;pid=$p.Id} | ConvertTo-Json -Depth 4 -Compress | Add-Content "$out/events.jsonl"
 }
 while(!$p.HasExited -and $clock.Elapsed.TotalSeconds -lt $seconds){[void]$p.WaitForExit(100)}
}catch{$failure=$_.Exception.Message}finally{
 if($p){
  $exitedBeforeCleanup=$p.HasExited
  if(!$p.HasExited){
   [void]$p.CloseMainWindow()
   if(!$p.WaitForExit(5000)){$forced=$true;Stop-ProcessTree $p;[void]$p.WaitForExit(1000)}
  }
  if(!$failure -and $forced){$failure='Title required forced termination at cleanup'}
  if(!$failure -and $p.ExitCode -ne 0){$failure="Title exited with code $($p.ExitCode)"}
  $stdout.Result | Set-Content "$out/stdout.log";$stderr.Result | Set-Content "$out/stderr.log"
  @{pid=$p.Id;exit=$p.ExitCode;elapsed_seconds=$clock.Elapsed.TotalSeconds;completed_events=$completed;
    failure=$failure;exited_before_cleanup=$exitedBeforeCleanup;forced_cleanup=$forced;
    handoff_present=(Test-Path "$out/handoff.bin");cleanup_is_not_xbox_home=$true} |
      ConvertTo-Json | Set-Content "$out/result.json"
  $p.Dispose()
 }
}
if($failure){throw $failure}
