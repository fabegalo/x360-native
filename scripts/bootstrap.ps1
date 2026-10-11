# One-time setup after cloning: SDK submodules, pinned LLVM, symlink fix-up.
# No admin rights; nothing outside this repository is modified.
# Usage: powershell -ExecutionPolicy Bypass -File scripts/bootstrap.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

git -C $root submodule update --init --recursive --jobs 8
if ($LASTEXITCODE -ne 0) { throw "git submodule update failed ($LASTEXITCODE)" }

& (Join-Path $PSScriptRoot 'bootstrap-llvm.ps1')
& (Join-Path $PSScriptRoot 'materialize-symlinks.ps1')

Write-Host ""
Write-Host "Next:"
Write-Host "  scripts\devshell.cmd cmake --preset win-amd64"
Write-Host "  scripts\devshell.cmd cmake --build --preset win-amd64-debug"
Write-Host "  scripts\devshell.cmd ctest --preset win-amd64-debug -j 16"
