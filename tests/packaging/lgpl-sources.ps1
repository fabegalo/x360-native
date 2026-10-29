$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $repo 'scripts/packaging/lgpl-sources.ps1')
$scratch=Join-Path $repo ('.local/evidence/import-17559/lgpl-'+[Guid]::NewGuid().ToString('N'))
$package=Join-Path $scratch 'package';$payload=Join-Path $scratch 'payload';$sdk=Join-Path $scratch 'sdk'
$entries=@()
foreach ($name in @('lzxd.c','lzx.h','system.h','mspack.h','macros.h','readbits.h','readhuff.h')) {
    $item=[pscustomobject]@{package_path="licenses/sources/libmspack/$name";sdk_path="thirdparty/libmspack/libmspack/mspack/$name";sha256=''}
    foreach ($base in @($package,$payload,$sdk)) {
        $rel=if($base -eq $sdk){$item.sdk_path}else{$item.package_path}
        $path=Join-Path $base $rel
        New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
        [IO.File]::WriteAllText($path,"synthetic corresponding source $name")
        $item.sha256=(Get-FileHash $path).Hash.ToLowerInvariant()
    }
    $entries+=$item
}
$inventory=[pscustomobject]@{lgpl_sources=$entries}
if (@(Assert-LgplSources $inventory $package $payload $sdk).Count -ne 7) {throw 'Positive source gate failed'}
$checks=1
function Reject($Action,[string]$Expected) {
    try { & $Action | Out-Null } catch {
        if (-not $_.Exception.Message.Contains($Expected)) {throw}
        $script:checks++;return
    }
    throw "Expected rejection: $Expected"
}
$inventory.lgpl_sources=@();Reject {Assert-LgplSources $inventory $package $payload $sdk} 'Missing LGPL'
$inventory.lgpl_sources=$entries
$original=$entries[0].package_path;$entries[0].package_path='../escape';Reject {Assert-LgplSources $inventory $package $payload $sdk} 'Unsafe';$entries[0].package_path=$original
$original=$entries[0].sdk_path;$entries[0].sdk_path='thirdparty/other/lzxd.c';Reject {Assert-LgplSources $inventory $package $payload $sdk} 'Unsafe';$entries[0].sdk_path=$original
$inventory.lgpl_sources=@($entries[0]) + @($entries[0]) + @($entries[2..6]);Reject {Assert-LgplSources $inventory $package $payload $sdk} 'Unsafe';$inventory.lgpl_sources=$entries
$original=$entries[0].sha256;$entries[0].sha256='bad';Reject {Assert-LgplSources $inventory $package $payload $sdk} 'Unsafe';$entries[0].sha256=$original
foreach ($base in @($package,$payload,$sdk)) {
    $rel=if($base -eq $sdk){$entries[0].sdk_path}else{$entries[0].package_path}
    $file=Join-Path $base $rel;$bytes=[IO.File]::ReadAllBytes($file)
    [IO.File]::WriteAllText($file,'tampered')
    Reject {Assert-LgplSources $inventory $package $payload $sdk} $(if($base -eq $sdk){'provenance'}else{'corresponding source'})
    [IO.File]::WriteAllBytes($file,$bytes)
    [IO.File]::Move($file,"$file.saved")
    Reject {Assert-LgplSources $inventory $package $payload $sdk} 'Missing required file'
    [IO.File]::Move("$file.saved",$file)
}
Write-Output "LGPL corresponding-source gate: $checks focused checks passed; synthetic fixtures in $scratch"
