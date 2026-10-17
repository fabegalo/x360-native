# Bounded diagnostic execution. Captures the guest GPU through GuestCapture;
# no desktop input, PrintWindow, UI automation, or acceptance by exit code.
param(
 [Parameter(Mandatory)][string]$Executable,
 [Parameter(Mandatory)][string]$GameRoot,
 [Parameter(Mandatory)][string]$UserData,
 [Parameter(Mandatory)][string]$OutputDirectory,
 [ValidateRange(1,17)][int]$Language=9,
 [ValidateRange(1,110)][int]$Country=13,
 [ValidateRange(10,120)][int]$Seconds=35,
 [ValidateRange(0,120)][int]$GuideAfter=14,
 [switch]$Dashboard,
 [string]$SystemUiHost,
 [string]$SystemUiRoot
)
$ErrorActionPreference='Stop'
$root= (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$exe=(Resolve-Path -LiteralPath $Executable).Path
$game=(Resolve-Path -LiteralPath $GameRoot).Path
$user=(Resolve-Path -LiteralPath $UserData).Path
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(!$exe.StartsWith("$root\.local\build\",[StringComparison]::OrdinalIgnoreCase)){throw 'Use a local build'}
if(!$out.StartsWith("$root\.local\evidence\validation\",[StringComparison]::OrdinalIgnoreCase)){throw 'Use .local/evidence/validation for evidence'}
if(Test-Path -LiteralPath $out){throw 'Evidence directory must be new'}
if($Language -eq 10){throw 'Reserved console language ID'}
if([bool]$SystemUiHost -ne [bool]$SystemUiRoot){throw 'Supply both original system UI host and root'}
if(Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($exe)) -ErrorAction SilentlyContinue){throw 'Title already running'}
New-Item -ItemType Directory -Path $out | Out-Null
$arguments=@('--gpu_plugin=xenos',"--user_language=$Language","--user_country=$Country",
 "--game_data_root=$game","--user_data_root=$user","--cache_root=$user/cache",
 "--x360n_profile_root=$user","--x360n_guest_capture=$out/frames",
 "--x360n_guest_capture_seconds=$Seconds","--log_file=$out/rex.log")
if($Dashboard){$arguments+=@('--dash17559_boot_intro=false',"--dash17559_guide_probe_seconds=$GuideAfter")}
if($SystemUiHost){
 $arguments+=@("--x360n_system_ui_host=$((Resolve-Path $SystemUiHost).Path)",
 "--x360n_system_ui_root=$((Resolve-Path $SystemUiRoot).Path)")
}
$si=[Diagnostics.ProcessStartInfo]::new()
$si.FileName=$exe;$si.WorkingDirectory=$out;$si.UseShellExecute=$false;$si.CreateNoWindow=$true
$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true
foreach($arg in $arguments){$si.ArgumentList.Add($arg)}
@{diagnostic=$true;production_receipt_validated=$false;language=$Language;country=$Country;
 arguments=$arguments;head=(& git -C $root rev-parse HEAD).Trim();
 lock_sha256=(Get-FileHash "$root/deps.lock.json").Hash;
 binaries=@(Get-ChildItem (Split-Path $exe) -File | Where-Object Extension -in '.exe','.dll' | ForEach-Object {
   @{path=$_.FullName;sha256=(Get-FileHash $_.FullName).Hash}
 })} | ConvertTo-Json -Depth 6 | Set-Content "$out/environment.json"
$p=$null;$clock=[Diagnostics.Stopwatch]::StartNew();$bounded=$false
try {
 $p=[Diagnostics.Process]::Start($si)
 $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync()
 if(!$p.WaitForExit($Seconds*1000)){$bounded=$true;$p.Kill($true);if(!$p.WaitForExit(5000)){throw 'Diagnostic cleanup failed'}}
 $stdout.Result | Set-Content "$out/stdout.log";$stderr.Result | Set-Content "$out/stderr.log"
 @{exit=$p.ExitCode;bounded_stop=$bounded;elapsed_seconds=$clock.Elapsed.TotalSeconds;
   screenshots=@(Get-ChildItem "$out/frames" -Recurse -Filter '*.ppm' -ErrorAction SilentlyContinue).Count;
   clean_shutdown_verified=$false} | ConvertTo-Json | Set-Content "$out/result.json"
} finally {
 if($p){if(!$p.HasExited){$p.Kill($true);[void]$p.WaitForExit(5000)};$p.Dispose()}
}
