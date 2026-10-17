#requires -Version 7.0
param([Parameter(Mandatory)][string]$Executable,
      [Parameter(Mandatory)][string]$Output)
$ErrorActionPreference='Stop'
if(Test-Path $Output){throw 'Refusing to overwrite benchmark evidence'}
if(Get-Process -Name dash_17559,castle_crashers,forza_horizon -ErrorAction SilentlyContinue){throw 'Wait for title sessions to finish before measuring'}
$exe=(Resolve-Path $Executable).Path
$si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$exe
$si.WorkingDirectory=Split-Path $exe;$si.UseShellExecute=$false;$si.CreateNoWindow=$true
$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true
$p=[Diagnostics.Process]::Start($si)
$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync()
try {
 if(!$p.WaitForExit(30000)){throw 'Counter benchmark exceeded 30 seconds'}
 if($p.ExitCode){throw "Counter benchmark failed: $($stderr.Result)"}
 $result=$stdout.Result | ConvertFrom-Json -AsHashtable
 $result['utc']=[DateTime]::UtcNow.ToString('o')
 $result['probe_sha256']=(Get-FileHash $exe).Hash.ToLower()
 $result['runtime']=@(Get-ChildItem (Split-Path $exe) -Filter 'rexruntime*.dll' | ForEach-Object {@{file=$_.Name;sha256=(Get-FileHash $_.FullName).Hash.ToLower()}})
 $result['logical_processors']=[Environment]::ProcessorCount
 $result['description']='Real runtime counter calls; synthetic independent counters, not game FPS'
 $result | ConvertTo-Json -Depth 8 | Set-Content $Output
} finally {if(!$p.HasExited){$p.Kill($true);[void]$p.WaitForExit(5000)}}
