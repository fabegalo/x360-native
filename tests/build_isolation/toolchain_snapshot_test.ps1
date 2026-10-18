param([string]$Ninja, [string]$Root = (Resolve-Path "$PSScriptRoot/../..").Path, [string]$SnapshotScript)
$ErrorActionPreference = 'Stop'
$env:PSModulePath = Join-Path $PSHOME 'Modules'
. "$Root/scripts/build/preparation-receipt.ps1"
if (-not $SnapshotScript) { $SnapshotScript = "$Root/scripts/build/toolchain-snapshot.ps1" }
$scratch = Join-Path $Root ('.local/build/toolchain-test-' + [Guid]::NewGuid().ToString('N'))
$include = Join-Path $scratch 'include with spaces'
$libraries = Join-Path $scratch 'libraries'
function Assert($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Snapshot {
    & "$Root/scripts/devshell.cmd" powershell -NoProfile -ExecutionPolicy Bypass -File "$scratch/capture.ps1" *> "$scratch/capture.log"
    if ($LASTEXITCODE) { throw ([IO.File]::ReadAllText("$scratch/capture.log")) }
    Get-ReceiptHash "$scratch/.preparation/toolchain.json"
}
try {
    [void][IO.Directory]::CreateDirectory($include)
    [void][IO.Directory]::CreateDirectory($libraries)
    [void][IO.Directory]::CreateDirectory("$scratch/source")
    [IO.File]::WriteAllText("$scratch/source/input.cpp", '// source')
    [IO.File]::WriteAllText("$include/header.h", 'header')
    [IO.File]::WriteAllText("$libraries/implicit.lib", 'library v1')
    [IO.File]::WriteAllText("$scratch/CMakeCache.txt", "CMAKE_MAKE_PROGRAM:FILEPATH=$Ninja`nCMAKE_HOME_DIRECTORY:INTERNAL=$scratch`n")
    [IO.File]::WriteAllText("$scratch/CMakeLists.txt", 'set(TEST "$ENV{X360N_BUILD_FLAG_TEST}")')
    [IO.File]::WriteAllText("$scratch/build.ninja", "rule compiler`n  command = unused-compiler -I`"$include`"`nbuild dummy.obj: compiler source/input.cpp`nbuild build.ninja: phony CMakeLists.txt`n")
    [IO.File]::WriteAllText("$scratch/capture.ps1", @"
`$ErrorActionPreference = 'Stop'
`$env:LIB = '$libraries;' + `$env:LIB
`$env:X360N_BUILD_FLAG_TEST = 'compiler-semantics-one'
& '$SnapshotScript' -Build '$scratch'
"@)
    $first = Snapshot
    Assert ((Snapshot) -eq $first) 'identical devshell/search inventory was not stable'
    $snapshot = [IO.File]::ReadAllText("$scratch/.preparation/toolchain.json") | ConvertFrom-Json
    Assert ($snapshot.graph_trusted -and ([IO.Path]::GetFullPath("$libraries/implicit.lib")) -in @($snapshot.libraries)) 'implicit library not captured'
    [IO.File]::WriteAllText("$include/shadow", 'new extensionless header')
    Assert ((Snapshot) -ne $first) 'new header in an explicit include directory was not detected'
    $extensionlessInventory = Get-ReceiptHash "$scratch/.preparation/toolchain.json"
    [IO.File]::WriteAllText("$include/shadow.custom", 'new include with arbitrary extension')
    Assert ((Snapshot) -ne $extensionlessInventory) 'include with arbitrary extension was ignored'
    $customInventory = Get-ReceiptHash "$scratch/.preparation/toolchain.json"
    [IO.File]::WriteAllText("$scratch/source/quoted", 'new local quote include')
    Assert ((Snapshot) -ne $customInventory) 'new local quoted header was ignored'
    $headerInventory = Get-ReceiptHash "$scratch/.preparation/toolchain.json"
    $capture = [IO.File]::ReadAllText("$scratch/capture.ps1").Replace('compiler-semantics-one','compiler-semantics-two')
    [IO.File]::WriteAllText("$scratch/capture.ps1", $capture)
    Assert ((Snapshot) -ne $headerInventory) 'environment variable referenced by CMake was ignored'
    Assert (-not [IO.File]::ReadAllText("$scratch/.preparation/toolchain.json").Contains('compiler-semantics-two')) 'raw environment value persisted'
    $librariesBefore = @(Get-ReceiptEntries $snapshot.libraries)
    $time = [IO.File]::GetLastWriteTimeUtc("$libraries/implicit.lib")
    [IO.File]::WriteAllText("$libraries/implicit.lib", 'library v2')
    [IO.File]::SetLastWriteTimeUtc("$libraries/implicit.lib", $time)
    Assert (-not (Test-ReceiptEntries $librariesBefore)) 'implicit library substitution accepted'
    Write-Output 'PASS: deterministic devshell, include paths with spaces, new shadow headers, implicit libraries, CMake environment identity without raw values'
} finally {
    $resolved = [IO.Path]::GetFullPath($scratch)
    $boundary = [IO.Path]::GetFullPath((Join-Path $Root '.local/build')).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($boundary, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected scratch path' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
