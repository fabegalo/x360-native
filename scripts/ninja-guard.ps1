# Invoked by CMAKE_MAKE_PROGRAM, including cmake --build and graph queries.
# Keep the pinned Ninja executable and all arguments; only ownership is added.
$ErrorActionPreference = 'Stop'
$env:PSModulePath = Join-Path $PSHOME 'Modules'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$root, $build, $title, $ninja = $args[0..3]
if ($title -eq '__root__') { $title = '' }
$ninjaArgs = @($args | Select-Object -Skip 4)
. (Join-Path $PSScriptRoot 'build-lease.ps1')
$lease = $null
$previousOwnedBuild = $env:X360N_OWNED_BUILD
try {
    if ($env:X360N_OWNED_BUILD -ne $build) {
        $lease = Enter-BuildLease $root $build $title
        # Ninja can regenerate CMake, which queries Ninja recursively. Only
        # descendants of this owner inherit the lease; another process rejects.
        $env:X360N_OWNED_BUILD = $build
    }
    # Direct builds cannot publish a receipt. Conservatively keep the existing
    # marker until build-title verifies the complete dependency/artifact closure.
    $query = $ninjaArgs -contains '--version' -or $ninjaArgs -contains '-n'
    $toolIndex = [Array]::IndexOf($ninjaArgs, '-t')
    if ($toolIndex -ge 0 -and $ninjaArgs[$toolIndex + 1] -ne 'clean') { $query = $true }
    $marker = Join-Path $root ".local/preparation/$title.incomplete"
    $hadMarker = [IO.File]::Exists($marker)
    if ($title -and -not $query) {
        [IO.File]::WriteAllText($marker, 'incomplete')
    }
    & $ninja @ninjaArgs
    $result = $LASTEXITCODE
    if ($result -eq 0 -and $title -and -not $query -and -not $hadMarker -and $lease) {
        # A no-op direct build may retain its already certified preparation.
        # Changed or partially staged products always require build-title.
        . (Join-Path $PSScriptRoot 'preparation-receipt.ps1')
        $exe = Join-Path $build "$title.exe"
        $manifest = Join-Path $root "titles/$title/${title}_manifest.toml"
        $receipt = Get-PreviousReceipt "$exe.preparation.json" $title $manifest $exe
        if ($receipt -and (Test-ReceiptEntries @($receipt.inputs + $receipt.artifacts)) -and (Test-ReceiptGlobs $receipt.globs)) {
            [IO.File]::Delete($marker)
        }
    }
    exit $result
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 73
} finally {
    $env:X360N_OWNED_BUILD = $previousOwnedBuild
    if ($lease) { foreach ($handle in $lease) { $handle.Dispose() } }
}
