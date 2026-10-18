param([Parameter(Mandatory)][string]$Executable, [string]$OutputDirectory)
$ErrorActionPreference='Stop'
$temporary=-not $OutputDirectory
if($temporary){$OutputDirectory=Join-Path ([IO.Path]::GetTempPath()) ('x360n-camera-'+[Guid]::NewGuid().ToString('N'))}
$out=[IO.Path]::GetFullPath($OutputDirectory)
[void][IO.Directory]::CreateDirectory($out)
try {
    foreach($operation in @('contract','SetCaptureMode','GetConfig','SetConfig','ReadFrame','Snapshot','SetView','GetView','Destroy','Reset')) {
        $si=[Diagnostics.ProcessStartInfo]::new([IO.Path]::GetFullPath($Executable))
        $si.UseShellExecute=$false;$si.CreateNoWindow=$true
        $si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true
        if($operation -ne 'contract'){$si.Arguments=$operation}
        $p=[Diagnostics.Process]::Start($si)
        $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync()
        try {
            if(!$p.WaitForExit(5000)){throw "Camera subprocess timed out: $operation"}
            $text=$stdout.Result+$stderr.Result
            [IO.File]::WriteAllText((Join-Path $out "$operation.log"),$text)
            [IO.File]::WriteAllText((Join-Path $out "$operation.exit.txt"),[string]$p.ExitCode)
            if($operation -eq 'contract') {
                if($p.ExitCode -ne 0 -or !$text.Contains('PASS: camera disconnected')){throw 'Camera unavailable contract failed'}
            } elseif($p.ExitCode -ne 3 -or $text.Contains('unsupported operation returned')) {
                throw "Unsupported operation did not abort explicitly: $operation exit=$($p.ExitCode)"
            }
        }finally{if(!$p.HasExited){taskkill /PID $p.Id /T /F | Out-Null};$p.Dispose()}
    }
    Write-Output 'PASS: 11 exports linked; state/create contract preserved; nine unsupported operations abort'
}finally{
    if($temporary){
        $boundary=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
        if(!$out.StartsWith($boundary,[StringComparison]::OrdinalIgnoreCase) -or
           [IO.Path]::GetFileName($out) -notmatch '^x360n-camera-[0-9a-f]{32}$'){throw 'Unsafe temporary cleanup path'}
        Remove-Item -LiteralPath $out -Recurse -Force
    }
}
