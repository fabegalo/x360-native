param([string]$Root = (Resolve-Path "$PSScriptRoot/../..").Path)
$ErrorActionPreference = 'Stop'
$title = 'aot_lease_probe_' + [Guid]::NewGuid().ToString('N')
$source = Join-Path $Root ".local/aot/$title"
$build = Join-Path $Root ".local/build/$title"
$marker = Join-Path $Root ".local/preparation/$title.incomplete"
[void][IO.Directory]::CreateDirectory($source)
try {
    $module = (Join-Path $Root 'cmake/x360n_build_isolation.cmake').Replace('\','/')
    [IO.File]::WriteAllText((Join-Path $source 'CMakeLists.txt'), @"
cmake_minimum_required(VERSION 3.25)
project($title NONE)
include("$module")
x360n_guard_ninja()
add_custom_target(probe ALL COMMAND "`${CMAKE_COMMAND}" -E touch "`${CMAKE_BINARY_DIR}/ran")
"@)
    & cmake -S $source -B $build -G Ninja
    if ($LASTEXITCODE) { throw 'Generic fixture configure failed' }
    & cmake --build $build
    if ($LASTEXITCODE -or -not [IO.File]::Exists($marker)) { throw 'Direct generic build did not invalidate its title receipt' }
    . "$Root/scripts/build-lease.ps1"
    $lease = Enter-BuildLease $Root (Join-Path $Root '.local/build') $title
    try {
        & cmake --build $build
        if ($LASTEXITCODE -eq 0) { throw 'Direct generic build ignored another producer of the same title' }
    } finally { foreach ($handle in $lease) { $handle.Dispose() } }
    Write-Output 'PASS: generated project direct builds invalidate receipts and exclude concurrent title producers'
} finally {
    foreach ($path in @($source,$build)) {
        $resolved = [IO.Path]::GetFullPath($path)
        if (-not $resolved.StartsWith((Join-Path $Root '.local\'), [StringComparison]::OrdinalIgnoreCase) -or
            [IO.Path]::GetFileName($resolved) -ne $title) { throw 'Unsafe fixture cleanup path' }
        if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
    }
    foreach ($suffix in @('incomplete','lock')) {
        [IO.File]::Delete((Join-Path $Root ".local/preparation/$title.$suffix"))
    }
}
