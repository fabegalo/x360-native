$ErrorActionPreference='Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $root 'scripts/packaging/clear-package-intermediates.ps1')
$scratch = Join-Path $root ('.local/tmp/package-cleanup-' + [guid]::NewGuid().ToString('N'))
$job = Join-Path $scratch ('.local/build/distribution/' + [guid]::NewGuid().ToString('N'))
$build = Join-Path $job 'build'
New-Item -ItemType Directory -Force "$build/CMakeFiles/test.dir", "$build/src/desktop" | Out-Null
[IO.File]::WriteAllText("$build/CMakeFiles/test.dir/a.obj",'compiler fixture')
foreach ($name in @('x360-native.exe','x360n.exe','x360n-extract.exe','rexruntime.dll')) { [IO.File]::WriteAllText("$build/src/desktop/$name",'preserve fixture') }
$failed=$false
try { Clear-PackageIntermediates $scratch $build } catch { $failed=$true }
if (-not $failed -or -not (Test-Path "$build/CMakeFiles/test.dir/a.obj")) { throw 'Incomplete job was cleaned' }
'{"schema":1,"status":"complete"}' | Set-Content (Join-Path $job '.x360n-packaging-complete.json')
Clear-PackageIntermediates $scratch $build
if (Test-Path "$build/CMakeFiles/test.dir/a.obj") { throw 'Compiler fixture retained' }
foreach ($name in @('x360-native.exe','x360n.exe','x360n-extract.exe','rexruntime.dll')) { if (-not (Test-Path "$build/src/desktop/$name")) { throw "Product removed: $name" } }
$failed=$false
try { Clear-PackageIntermediates $scratch (Join-Path $scratch '.local/inputs') } catch { $failed=$true }
if (-not $failed) { throw 'Unapproved root accepted' }
# Keep a compact PASS receipt; remove only this successfully checked fixture.
$absolute = [IO.Path]::GetFullPath($scratch)
if (-not $absolute.StartsWith((Join-Path $root '.local/tmp/package-cleanup-'),[StringComparison]::OrdinalIgnoreCase)) { throw 'Fixture root mismatch' }
Remove-Item -LiteralPath $absolute -Recurse -Force
'PASS: incomplete jobs preserved; compiler cache removed after success; all Release products retained; roots constrained'
