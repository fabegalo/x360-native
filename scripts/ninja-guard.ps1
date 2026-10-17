# Compatibility entry point for Ninja wrappers in existing CMake build trees.
# Keep until those trees have been explicitly reconfigured by their owners.
& (Join-Path $PSScriptRoot 'build/ninja-guard.ps1') @args
exit $LASTEXITCODE
