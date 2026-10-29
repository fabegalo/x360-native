param([string]$Root=(Resolve-Path "$PSScriptRoot/../..").Path)
$ErrorActionPreference='Stop'
. "$Root/scripts/build/preparation-receipt.ps1"
. "$Root/scripts/build/incremental-state.ps1"
Initialize-ProductFingerprints
$build=Join-Path $Root '.local/build/dependency-inventory-fixture'
$lines=[Collections.Generic.List[string]]::new()
foreach($index in 1..300){
 $lines.Add("output-$index.obj: #deps 8, deps mtime 1 (VALID)")
 foreach($path in @('relative/header.h','relative/../relative/header.h','folder with spaces/á.h',"$Root/absolute.h",'relative/other.hpp','relative/HEADER.h')){$lines.Add('    '+$path)}
 $lines.Add('')
}
$deps=$lines -join "`n"
$expected=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach($line in ($deps -split '\r?\n')){if($line.StartsWith('    ')){[void]$expected.Add((Resolve-BuildPath $build $line.Substring(4)))}}
$actual=[X360NProducts]::AllCompilerDependencies($build,$deps)
if((@($expected | Sort-Object) -join "`n") -ne (@($actual | Sort-Object) -join "`n")){throw 'Native inventory differs from original parser'}
$selected=[X360NProducts]::CompilerDependencies($build,$deps,[string[]]@((Join-Path $build 'output-2.obj')))
if(($selected -join "`n") -ne ($actual -join "`n")){throw 'Object-filtered dependency contract changed'}
Write-Output 'PASS: duplicate deps, relative/absolute paths, Unicode/spaces, normalization and original object filtering'
