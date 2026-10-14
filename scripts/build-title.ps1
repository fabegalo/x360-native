# Builds a title project under titles/<name> from scratch, including codegen.
#
# With the v0.10.0 init template in REXSDK_DIR mode the rexglue tool is built in
# the same tree, and generated/<module>/sources.cmake is only included if it
# exists at configure time (generated/rexglue.cmake). A single configure+build
# on a tree without generated code therefore runs codegen but links without the
# guest code ("undefined symbol ... PPCImageConfig"), and rebuilding does not
# re-run CMake. Hence four steps:
#   1 configure   2 build <name>_codegen   3 configure again   4 build
# Each step's output goes to <BuildDir>/x360n-build-<step>.log; stops at the first failure.
# Usage: scripts/build-title.ps1 -Title iruka [-BuildDir .local/build/titles/iruka]
param(
    [Parameter(Mandatory)] [string]$Title,
    [string]$BuildDir,
    [switch]$KeepIncompleteMarker
)
$ErrorActionPreference = 'Stop'
# Preparation uses only modules shipped with the selected PowerShell host.
$env:PSModulePath = Join-Path $PSHOME 'Modules'
# Ninja emits UTF-8 dependency paths, including on Windows PowerShell 5.1.
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$root = (Resolve-Path (Split-Path -Parent $PSScriptRoot)).Path
. (Join-Path $PSScriptRoot 'preparation-receipt.ps1')
. (Join-Path $PSScriptRoot 'build-lease.ps1')
$driverSnapshot = @(Get-ReceiptEntries @($PSCommandPath, (Join-Path $PSScriptRoot 'preparation-receipt.ps1'), (Join-Path $PSScriptRoot 'build-lease.ps1')))
if ($Title -notmatch '^[a-z][a-z0-9_]*$') { throw "invalid title name '$Title'" }
$titleDir = Join-Path $root "titles\$Title"
if (-not (Test-Path (Join-Path $titleDir 'CMakePresets.json'))) {
    $titleDir = Join-Path $root ".local/aot/$Title"
}
if (-not (Test-Path (Join-Path $titleDir 'CMakePresets.json'))) { throw "no title project at $titleDir" }
$ownerSnapshot = @()
if ($titleDir.StartsWith((Join-Path $root '.local\aot\'), [StringComparison]::OrdinalIgnoreCase)) {
        $preset = Get-Content -LiteralPath (Join-Path $titleDir 'CMakePresets.json') -Raw | ConvertFrom-Json
        $declared = @($preset.configurePresets | Where-Object { $_.name -eq 'x360n-win-amd64' })
        if ($declared.Count -ne 1 -or -not $declared[0].binaryDir) { throw 'Generated AOT build directory missing' }
        $declaredBuild = [IO.Path]::GetFullPath($declared[0].binaryDir)
        if (-not $declaredBuild.StartsWith((Join-Path $root '.local\build\'), [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Generated AOT build directory escaped the environment'
        }
        $ownerFile = Join-Path $declaredBuild '.x360n-aot-owner'
        if (-not [IO.File]::Exists($ownerFile) -or [IO.File]::ReadAllLines($ownerFile)[0] -ne $Title) {
            throw 'Generated AOT build identity missing or different; regenerate the project before building.'
        }
        $ownerSnapshot = @(Get-ReceiptEntries @($ownerFile))
        if (-not $BuildDir) { $BuildDir = $declaredBuild }
}
if (-not $BuildDir) { $BuildDir = ".local/build/titles/$Title" }
$build = if ([IO.Path]::IsPathRooted($BuildDir)) { [IO.Path]::GetFullPath($BuildDir) }
         else { [IO.Path]::GetFullPath((Join-Path $root $BuildDir)) }
New-Item -ItemType Directory -Force $build | Out-Null
$lease = Enter-BuildLease $root $build $Title
$previousOwnedBuild = $env:X360N_OWNED_BUILD
$env:X360N_OWNED_BUILD = $build
$marker = Join-Path $root ".local/preparation/$Title.incomplete"
New-Item -ItemType Directory -Force (Split-Path $marker) | Out-Null
[IO.File]::WriteAllText($marker, 'incomplete')
$devshell = Join-Path $root 'scripts\devshell.cmd'
$exe = Join-Path $build "$Title.exe"
$manifest = Join-Path $titleDir "${Title}_manifest.toml"
$receiptFile = "$exe.preparation.json"
$previous = Get-PreviousReceipt $receiptFile $Title $manifest $exe
$reuse = $previous -and (Test-ReceiptEntries @($previous.inputs + $previous.artifacts)) -and (Test-ReceiptGlobs $previous.globs)
if ($reuse) {
    $head = & git -C (Join-Path $root 'external/rexglue-sdk') rev-parse HEAD
    $reuse = $LASTEXITCODE -eq 0 -and $head.Trim() -eq $previous.sdk_commit
}
$sourceSnapshot = @()

$steps = @(
    @{ name = '1-configure';  args = "cmake --preset x360n-win-amd64 -B `"$build`" -DX360N_CODEGEN_BOOTSTRAP=ON" },
    @{ name = '2-tool';       args = "cmake --build `"$build`" --target rexglue" },
    @{ name = '2-codegen';    args = '' },
    @{ name = '3-reconfigure'; args = "cmake --preset x360n-win-amd64 -B `"$build`" -DX360N_CODEGEN_BOOTSTRAP=OFF" },
    @{ name = '4-build';      args = "cmake --build `"$build`"" }
)
Push-Location $titleDir
try {
    if ($reuse) {
        # A verified final build needs no relink from unverified intermediates or
        # mutable shared SDK outputs. Keep the exact certified producer/products.
        if (-not $KeepIncompleteMarker) { Remove-Item -LiteralPath $marker }
        [IO.File]::Delete((Join-Path $root ".local/preparation/$Title.runtime-error"))
        Write-Host "reused verified preparation: $exe"
        return
    }
    foreach ($s in $steps) {
        if ($s.name -eq '2-tool') {
            $cache = Get-BuildCache $build
            if (-not $reuse) {
                # Native objects cannot be trusted after an uncertified build or
                # a content change with preserved mtime. Keep SDK codegen stamps:
                # its per-module input hashes still select the necessary modules.
                $rules = @(& $cache.CMAKE_MAKE_PROGRAM -C $build -t rules | Where-Object { $_ -match '(_COMPILER__|_LINKER__)' })
                & $cache.CMAKE_MAKE_PROGRAM -C $build -t clean -r @rules
                if ($LASTEXITCODE) { throw 'Cannot invalidate stale native products' }
                # Embedded templates and other custom outputs also require a
                # content-safe rebuild when input mtimes were preserved.
                $generated = @(& $cache.CMAKE_MAKE_PROGRAM -C $build -t targets rule CUSTOM_COMMAND)
                if ($LASTEXITCODE) { throw 'Cannot identify generated build products' }
                Reset-GeneratedBuildOutputs $build $generated
            }
            $sourceSnapshot = @(Get-ReceiptEntries @(Get-BuildInputs $root $Title $build @($Title) | Where-Object {
                [IO.File]::Exists($_) -and
                -not $_.StartsWith($build, [StringComparison]::OrdinalIgnoreCase) -and
                -not $_.StartsWith((Join-Path $titleDir 'generated'), [StringComparison]::OrdinalIgnoreCase)
            }))
        }
        if ($s.name -eq '2-codegen') {
            $tool = @(Get-NinjaNodes $build @('rexglue') | Where-Object { $_ -match '[\\/]rexglue[^\\/]*\.exe$' })[0]
            if (-not $tool) { throw 'Codegen executable missing from build graph' }
            # Keep the exact producer used by this preparation. The SDK's shared
            # output directory is replaced by other title builds (C5); provenance
            # must not depend on that mutable staging location. Its source and
            # compiler dependency closure is still checked by the receipt.
            $producerDir = Join-Path $build '.preparation/codegen'
            [void][IO.Directory]::CreateDirectory($producerDir)
            $producerFiles = @($tool) + @(Get-NinjaNodes $build @('rexglue') | Where-Object { $_ -match '\.dll$' -and [IO.File]::Exists($_) })
            $copies = foreach ($file in $producerFiles) {
                $copy = Join-Path $producerDir ([IO.Path]::GetFileName($file))
                if (-not [IO.File]::Exists($copy) -or (Get-ReceiptHash $copy) -ne (Get-ReceiptHash $file)) { [IO.File]::Copy($file, $copy, $true) }
                $copy
            }
            $tool = Join-Path $producerDir ([IO.Path]::GetFileName($tool))
            $producer = @(Get-ReceiptEntries $copies)
            $sameProducer = $previous -and $previous.producer -and (Test-ReceiptEntries $previous.producer)
            # Codegen's sdkVersion alone is not a producer identity. Force only
            # this title when the actual producer changes or has no provenance.
            $ignore = if ($sameProducer) { '' } else { '--ignore-stamp' }
            if ($sameProducer) {
                foreach ($entry in $previous.inputs) {
                    if (($entry.path.StartsWith((Join-Path $titleDir 'generated'), [StringComparison]::OrdinalIgnoreCase) -or
                         $entry.path.StartsWith((Join-Path $build 'generated'), [StringComparison]::OrdinalIgnoreCase)) -and
                        (-not [IO.File]::Exists($entry.path) -or (Get-ReceiptHash $entry.path) -ne $entry.sha256)) {
                        $stamp = Join-Path (Split-Path $entry.path) 'codegen.stamp'
                        if ([IO.File]::Exists($stamp)) { [IO.File]::Delete($stamp) }
                    }
                }
            }
            # Invoke even when Ninja's timestamp test says up-to-date: SDK hashes
            # the XEX and analysis and skips unchanged modules by content.
            $s.args = "`"$tool`" codegen `"$manifest`" $ignore"
        }
        $invocations = @($s)
        if ($s.name -eq '2-codegen') {
            $invocations += @(Get-AdditionalCodegenManifests $titleDir $build | ForEach-Object {
                @{ name = '2-codegen-' + [IO.Path]::GetFileNameWithoutExtension($_);
                   args = "`"$tool`" codegen `"$_`" $ignore" }
            })
        }
        foreach ($invocation in $invocations) {
            Write-Output "X360N_STAGE $Title $($invocation.name)"
            $log = Join-Path $build "x360n-build-$($invocation.name).log"
            [IO.File]::WriteAllText($marker, "Preparing: $($invocation.name)`nLog: $log")
            $sw = [Diagnostics.Stopwatch]::StartNew()
            # Redirect inside cmd: in PowerShell 5.1 with ErrorActionPreference=Stop,
            # native stderr (e.g. VsDevCmd's "'vswhere.exe' is not recognized") would
            # otherwise become a terminating NativeCommandError.
            cmd /c "`"$devshell`" $($invocation.args) > `"$log`" 2>&1"
            $rc = $LASTEXITCODE
            Write-Host ("step {0}: exit {1} in {2} s (log {3})" -f $invocation.name, $rc, [int]$sw.Elapsed.TotalSeconds, $log)
            if ($rc -ne 0) {
                [IO.File]::WriteAllText($marker, "Error: $($invocation.name), exit $rc`nLog: $log`n" + ((Get-Content $log -Tail 25) -join "`n"))
                Get-Content $log | Select-String -Pattern 'error|FAILED' | Select-Object -First 10 | Out-Host
                throw "Preparation failed at $($invocation.name), exit $rc"
            }
            }
        if ($s.name -in @('1-configure', '3-reconfigure')) {
            # CMake 3.31's __create_def calls CreateFileW without converting long
            # object paths (Source/bindexplib.cxx). Keep its generated object list
            # and symbol extraction; only spell paths >= MAX_PATH in extended form.
            # Configure regenerates these lists, so repeat after both configure steps.
            # Use CMake's target list: PowerShell 5's recursive directory walk
            # also rejects the long object directories that caused this failure.
            foreach ($directory in [IO.File]::ReadAllLines((Join-Path $build 'CMakeFiles/TargetDirectories.txt'))) {
                $list = Join-Path $directory 'exports.def.objs'
                if (-not [IO.File]::Exists($list)) { continue }
                $changed = $false
                $entries = foreach ($entry in [IO.File]::ReadAllLines($list)) {
                    if (-not $entry -or $entry.StartsWith('\\?\')) { $entry; continue }
                    $absolute = [IO.Path]::GetFullPath((Join-Path $build $entry))
                    if ($absolute.Length -lt 260) { $entry; continue }
                    $changed = $true
                    if ($absolute.StartsWith('\\')) { '\\?\UNC\' + $absolute.Substring(2) }
                    else { '\\?\' + $absolute }
                }
                if ($changed) {
                    [IO.File]::WriteAllLines($list, [string[]]$entries, [Text.UTF8Encoding]::new($false))
                    Write-Host "Extended object paths for CMake exports: $list"
                }
            }
        }
    }
    if (-not (Test-ReceiptEntries $driverSnapshot) -or -not (Test-ReceiptEntries $ownerSnapshot) -or
        -not (Test-ReceiptEntries $sourceSnapshot)) { throw 'An input changed during preparation; prepare again. No receipt published.' }
    $inputs = @(Get-ReceiptEntries @(Get-BuildInputs $root $Title $build @($Title)))
    $dllOutputs = @(Get-NinjaNodes $build @($Title) | Where-Object { $_ -match '\.dll$' } | Sort-Object -Unique)
    $artifacts = @(Get-PreparedArtifacts $build $exe $dllOutputs | Sort-Object path -Unique)
    $sdkCommit = (& git -C (Join-Path $root 'external/rexglue-sdk') rev-parse HEAD).Trim()
    if ($LASTEXITCODE) { throw 'Cannot identify SDK revision' }
    $globs = @(Get-BuildGlobs $build)
    if (-not (Test-ReceiptGlobs $globs)) { throw 'Build source inventory changed; prepare again. No receipt published.' }
    if (-not (Test-ReceiptEntries $sourceSnapshot) -or -not (Test-ReceiptEntries $producer)) {
        throw 'Inputs or codegen changed while collecting the receipt; prepare again. No receipt published.'
    }
    $receipt = @{ schema = 1; title = $Title; manifest = $manifest; exe = $exe; sdk_commit = $sdkCommit; globs = $globs;
        inputs = @($inputs + $producer + $ownerSnapshot | Sort-Object path -Unique); artifacts = $artifacts; producer = $producer;
        roles = @{ compiler = (Get-BuildCache $build).CMAKE_CXX_COMPILER; codegen = $tool;
            build_config = (Join-Path $build 'CMakeCache.txt') } }
    Publish-PreparationReceipt $receiptFile $receipt
    [IO.File]::Delete((Join-Path $root ".local/preparation/$Title.runtime-error"))
    if (-not $KeepIncompleteMarker) { Remove-Item -LiteralPath $marker }
} catch {
    [IO.File]::AppendAllText($marker, "`nError: $($_.Exception.Message)`n")
    throw
} finally {
    Pop-Location
    $env:X360N_OWNED_BUILD = $previousOwnedBuild
    foreach ($handle in $lease) { $handle.Dispose() }
}
Write-Host "built: $(Join-Path $build "$Title.exe")"
