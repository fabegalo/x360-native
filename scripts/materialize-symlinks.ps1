# Replaces git symlinks (mode 120000) in an allowlisted submodule with copies of
# their targets. Needed on Windows when the account cannot create symlinks
# (no Developer Mode, not elevated): git then checks symlinks out as text files
# containing the target path, and ReXGlue's thirdparty/CMakeLists.txt compiles
# libmspack/cabextract/mspack/lzxd.c, which is such a symlink.
#
# Each copy is verified to hash to the same git blob as the upstream target.
# Undo with: git -C <repo> checkout -- .
# Usage: powershell -ExecutionPolicy Bypass -File scripts/materialize-symlinks.ps1 [-Repos <rel path>...]
#   -Repos  repositories relative to the project root (default: the pinned SDK's libmspack).
#           Each must resolve inside the project.
param([string[]]$Repos = @('external/rexglue-sdk/thirdparty/libmspack'))
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Split-Path -Parent $PSScriptRoot)).Path

foreach ($rel in $Repos) {
    $repo = (Resolve-Path (Join-Path $root $rel)).Path
    if (-not $repo.StartsWith($root + [IO.Path]::DirectorySeparatorChar)) {
        throw "$rel resolves outside the project root"
    }
    $entries = git -C $repo ls-files -s | Where-Object { $_ -match '^120000 ' }
    $done = 0; $skipped = 0
    foreach ($line in $entries) {
        $path = ($line -split "`t", 2)[1]
        $linkFile = Join-Path $repo $path
        $target = (git -C $repo cat-file -p ":$path")
        $targetRel = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $linkFile) $target))
        if (-not $targetRel.StartsWith($repo + [IO.Path]::DirectorySeparatorChar)) {
            throw "Symlink $path -> $target resolves outside $repo"
        }
        $targetInRepo = $targetRel.Substring($repo.Length + 1).Replace('\', '/')
        $expected = (git -C $repo rev-parse "HEAD:$targetInRepo")
        $current = (git -C $repo hash-object -- $path)
        if ($current -eq $expected) { $skipped++; continue }
        Copy-Item -LiteralPath $targetRel -Destination $linkFile -Force
        $after = (git -C $repo hash-object -- $path)
        if ($after -ne $expected) { throw "Copy of $targetInRepo into $path does not match blob $expected" }
        $done++
    }
    Write-Host "$rel : $($entries.Count) symlinks, $done materialized, $skipped already materialized"
}
