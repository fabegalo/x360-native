# Real optimized compiler commands, plus C2's existing graph/hash machinery.
param([string]$Root = (Resolve-Path "$PSScriptRoot/../..").Path, [switch]$DeveloperEnvironment)
$ErrorActionPreference = 'Stop'
$env:PSModulePath = Join-Path $PSHOME 'Modules'
if (-not $DeveloperEnvironment) {
    & "$Root/scripts/devshell.cmd" powershell -NoProfile -ExecutionPolicy Bypass -File $PSCommandPath -Root $Root -DeveloperEnvironment
    exit $LASTEXITCODE
}
. "$Root/scripts/preparation-receipt.ps1"
function Assert($ok, $message) { if (-not $ok) { throw $message } }
$scratch = Join-Path $Root ('.local/build/ppc-contract-' + [Guid]::NewGuid().ToString('N'))
$compiler = "$Root/.local/toolchains/llvm-22.1.8/bin/clang++.exe"
foreach ($name in @('a', 'b')) {
    $source = "$scratch/$name-src"; $build = "$scratch/$name"
    New-Item -ItemType Directory -Force $source | Out-Null
    Copy-Item "$PSScriptRoot/*" $source
    & cmake -S $source -B $build -G Ninja "-DX360N_ROOT=$Root" "-DCMAKE_CXX_COMPILER=$compiler" -DCMAKE_BUILD_TYPE=Release *> "$scratch/$name-configure.log"
    Assert ($LASTEXITCODE -eq 0) "configure failed: $name"
    & cmake --build $build --parallel 4 --verbose *> "$scratch/$name-build.log"
    Assert ($LASTEXITCODE -eq 0) "build failed: $name"
    & "$build/host.exe"
    Assert ($LASTEXITCODE -eq 0) "host failed: $name"
    $commands = Get-Content "$build/compile_commands.json" -Raw | ConvertFrom-Json
    foreach ($c in $commands) {
        $isPpc = $c.file -notmatch 'host.cpp$'
        Assert (($c.command.Contains('-fno-strict-aliasing')) -eq $isPpc) "alias flag scope: $($c.file)"
        Assert (($c.command.Contains('-ffp-model=strict')) -eq $isPpc) "FP flag scope: $($c.file)"
        Assert ($c.command -match '-O3') "Release optimization lost: $($c.file)"
    }
}
# Exercise the production collector, including the formerly omitted phony
# CMake inputs, rather than inventing another preparation identity.
$a = "$scratch/a"; $b = "$scratch/b"
$inputsA = @(Get-BuildInputs $Root iruka $a @('host', 'module'))
$inputsB = @(Get-BuildInputs $Root iruka $b @('host', 'module'))
$option = [IO.Path]::GetFullPath("$scratch/a-src/local-options.cmake")
Assert ($option -in $inputsA) 'included compile options missing from receipt inputs'
$entriesA = @(Get-ReceiptEntries $inputsA)
$entriesB = @(Get-ReceiptEntries $inputsB)
$time = [IO.File]::GetLastWriteTimeUtc($option)
[IO.File]::WriteAllText($option, 'target_compile_options(guest PRIVATE -fwrapv)')
[IO.File]::SetLastWriteTimeUtc($option, $time)
Assert (-not (Test-ReceiptEntries $entriesA)) 'semantic flag change reused preparation A'
Assert (Test-ReceiptEntries $entriesB) 'independent preparation B invalidated'
& cmake -S "$scratch/a-src" -B $a *> "$scratch/reconfigure.log"
Assert ($LASTEXITCODE -eq 0) 'reconfigure failed'
& cmake --build $a --parallel 4 --verbose *> "$scratch/rebuild.log"
Assert ($LASTEXITCODE -eq 0) 'rebuild failed'
$rebuild = Get-Content "$scratch/rebuild.log" -Raw
Assert ($rebuild -match ' -c .*guest.cpp') 'affected guest was not recompiled'
Assert ($rebuild -notmatch ' -c .*module.cpp| -c .*host.cpp') 'unaffected native/module sources recompiled'
Assert (Test-ReceiptEntries $entriesB) 'building A changed B'
Write-Output "PASS: PPC entry/module/PCH flags, native isolation, optimized compilation, C2 semantic-option invalidation and independent preparation ($scratch)"
