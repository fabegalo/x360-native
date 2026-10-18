param([Parameter(Mandatory)][string]$Ninja)
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot/../..").Path
. "$root/scripts/build/preparation-receipt.ps1"
$scratch=Join-Path ([IO.Path]::GetTempPath()) ('x360n-clean-'+[Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($scratch)
try {
    $graph=[Collections.Generic.List[string]]::new()
    for($i=0;$i -lt 180;$i++) {
        $rule=('module_'+('long_dependency_'*8)+$i+'_COMPILER__native')
        $graph.Add("rule $rule`n  command = unused-compiler`nbuild output-$i.obj: $rule`n")
        [IO.File]::WriteAllText((Join-Path $scratch "output-$i.obj"),'stale object')
    }
    $graph.Add("rule custom`n  command = unused-generator`nbuild preserved.stamp: custom`n")
    [IO.File]::WriteAllText((Join-Path $scratch 'build.ninja'),($graph -join "`n"))
    [IO.File]::WriteAllText((Join-Path $scratch 'preserved.stamp'),'verified AOT fingerprint')
    Clear-NinjaNativeProducts $Ninja $scratch
    if(@(Get-ChildItem -LiteralPath $scratch -Filter '*.obj').Count) {throw 'Native outputs survived cleaning'}
    if([IO.File]::ReadAllText((Join-Path $scratch 'preserved.stamp')) -ne 'verified AOT fingerprint') {throw 'AOT stamp changed'}
    $failed=$false
    [IO.File]::WriteAllText((Join-Path $scratch 'build.ninja'),'invalid build syntax')
    try {Clear-NinjaNativeProducts $Ninja $scratch}catch{$failed=$true}
    if(!$failed){throw 'Ninja failure was ignored'}
    Write-Output 'PASS: all 180 native outputs removed; custom stamp preserved; Ninja failure propagated'
}finally{
    $resolved=[IO.Path]::GetFullPath($scratch)
    $tempBoundary=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    if(!$resolved.StartsWith($tempBoundary,[StringComparison]::OrdinalIgnoreCase) -or
       [IO.Path]::GetFileName($resolved) -notmatch '^x360n-clean-[0-9a-f]{32}$') {throw 'Unsafe temporary cleanup path'}
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
