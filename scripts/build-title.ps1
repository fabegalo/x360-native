# Compatibility entry point. The native supervisor owns all AOT stages and receipts.
param(
    [Parameter(Mandatory)] [string]$Title,
    [string]$BuildDir,
    [switch]$KeepIncompleteMarker,
    [string]$Supervisor = $env:X360N_SUPERVISOR
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Split-Path -Parent $PSScriptRoot)).Path
if (-not $Supervisor) {
    foreach ($relative in @('x360n.exe', '.local/build/desktop-win/src/supervisor/x360n.exe', '.local/build/desktop-only/src/supervisor/x360n.exe')) {
        $candidate = Join-Path $root $relative
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $Supervisor = $candidate; break }
    }
}
if (-not $Supervisor -or -not (Test-Path -LiteralPath $Supervisor -PathType Leaf)) {
    throw 'Native supervisor missing. Build x360n or pass -Supervisor <x360n.exe>.'
}
$arguments = @('--root', $root, 'prepare', $Title)
if ($BuildDir) { $arguments += @('--build-dir', $BuildDir) }
if ($KeepIncompleteMarker) { $arguments += '--keep-incomplete' }
# Only establish the pinned compiler / Windows SDK environment here.
& (Join-Path $PSScriptRoot 'devshell.cmd') $Supervisor @arguments
exit $LASTEXITCODE
