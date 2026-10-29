param([Parameter(Mandatory)][string]$CMake,[Parameter(Mandatory)][string]$Source,
    [Parameter(Mandatory)][string]$Directory,[ValidateRange(1,8)][int]$Jobs=8)
$ErrorActionPreference='Stop'
$running=[Collections.Generic.List[object]]::new()
$watch=[Diagnostics.Stopwatch]::StartNew()
try {
    $context=[IO.File]::ReadAllText((Join-Path $Directory 'context.cmake'))
    $match=[regex]::Match($context,'set\(PROBE_NAMES \[==\[(.*?)\]==\]\)',[Text.RegularExpressions.RegexOptions]::Singleline)
    if(-not $match.Success){throw 'Libc probe inventory missing'}
    $names=@($match.Groups[1].Value.Split(';') | Where-Object {$_})
    $Jobs=[Math]::Min([Math]::Min($Jobs,[Environment]::ProcessorCount),$names.Count)
    $logs=[Collections.Generic.List[string]]::new()
    for($index=0;$index -lt $Jobs;$index++) {
        $bucket=@(for($entry=$index;$entry -lt $names.Count;$entry+=$Jobs){$names[$entry]})
        $worker=Join-Path $Directory "worker-$index"
        [void][IO.Directory]::CreateDirectory($worker)
        $inputFile=Join-Path $worker 'context.cmake'
        [IO.File]::WriteAllText($inputFile,$context+"`nset(PROBE_NAMES [==[$($bucket -join ';')]==])`n")
        $arguments=@('-S',$Source,'-B',$worker,'-G','Ninja',"-DPROBE_CONTEXT=$inputFile")
        $quoted=foreach($argument in $arguments){'"'+[regex]::Replace([regex]::Replace($argument,'(\\*)"','$1$1\"'),'(\\+)$','$1$1')+'"'}
        $info=[Diagnostics.ProcessStartInfo]::new()
        $info.FileName=$CMake;$info.Arguments=$quoted -join ' '
        $info.UseShellExecute=$false;$info.CreateNoWindow=$true
        $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
        $process=[Diagnostics.Process]::new();$process.StartInfo=$info
        if(-not $process.Start()){throw 'Cannot start libc probe worker'}
        $running.Add(@{process=$process;stdout=$process.StandardOutput.ReadToEndAsync();stderr=$process.StandardError.ReadToEndAsync();worker=$worker})
    }
    $result=''
    foreach($item in $running) {
        while(-not $item.process.WaitForExit(250)){if($watch.Elapsed.TotalSeconds -gt 600){throw 'Libc probes exceeded 600 seconds'}}
        [IO.File]::WriteAllText((Join-Path $item.worker 'configure.log'),$item.stdout.GetAwaiter().GetResult()+$item.stderr.GetAwaiter().GetResult())
        if($item.process.ExitCode){throw "Libc probe configure failed: $($item.worker)"}
        $result += [IO.File]::ReadAllText((Join-Path $item.worker 'results.cmake'))
    }
    [IO.File]::WriteAllText((Join-Path $Directory 'results.cmake'),$result)
} finally {
    foreach($item in $running){
        if(-not $item.process.HasExited){
            try{$item.process.Kill()}catch{ } # A worker can exit between the handle query and Kill.
            [void]$item.process.WaitForExit(5000)
        }
        $item.process.Dispose()
    }
}
