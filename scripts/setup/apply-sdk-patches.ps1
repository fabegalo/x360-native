# Applies (or reverts) the local ReXGlue SDK patches listed in deps.lock.json
# ("rexglue_sdk.patches") to the external/rexglue-sdk checkout.
#
# The patches are only valid for the pinned commit; the script refuses any other
# HEAD. Each patch is applied with `git apply` (working tree only, nothing is
# committed in the submodule). Already-applied patches are detected with
# `git apply --reverse --check` and skipped, so the script is idempotent. After
# applying, every patched file must have the SHA-256 recorded in the lock, which
# is also what CMake checks at configure time (cmake/sdk/x360n_sdk_backports.cmake).
#
# Usage: powershell -ExecutionPolicy Bypass -File scripts/setup/apply-sdk-patches.ps1 [-Revert] [-Check]
#   -Check   only report the state; exit 1 if any patch is missing or a file differs
#   -Revert  reverse-apply the patches (newest first), back to the pinned sources
param([switch]$Revert, [switch]$Check)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$sdk = Join-Path $root 'external\rexglue-sdk'
$lock = Get-Content -Raw (Join-Path $root 'deps.lock.json') | ConvertFrom-Json
$entry = $lock.rexglue_sdk
$patches = @($entry.patches)
if ($patches.Count -eq 0) { Write-Host 'no SDK patches in deps.lock.json'; exit 0 }

$head = (git -C $sdk rev-parse HEAD).Trim()
if ($head -ne $entry.commit) {
    Write-Host "external/rexglue-sdk is at $head, the patches are for $($entry.commit); not touching it"
    exit 1
}

function FileHash([string]$rel) {
    $p = Join-Path $sdk $rel
    if (-not (Test-Path $p)) { return '' }
    (Get-FileHash -Algorithm SHA256 $p).Hash.ToLower()
}
function PatchPath($p) {
    $path = Join-Path $root $p.file
    $actual = (Get-FileHash -Algorithm SHA256 $path).Hash.ToLower()
    if ($actual -ne $p.sha256) { throw "$($p.file): SHA-256 $actual, lock says $($p.sha256)" }
    $path
}
# Runs git in the SDK checkout and returns its exit code. In Windows PowerShell
# 5.1 native stderr becomes a terminating error under ErrorActionPreference=Stop,
# even redirected, so the preference is relaxed around the call.
function GitSdk([string[]]$gitArgs, [switch]$Quiet) {
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & git -C $sdk @gitArgs 2>&1
        if (-not $Quiet) { $out | ForEach-Object { Write-Host "  $_" } }
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $old
    }
}
function IsApplied([string]$path) { (GitSdk @('apply', '--reverse', '--check', $path) -Quiet) -eq 0 }

$ordered = if ($Revert) { [array]::Reverse($patches); $patches } else { $patches }
$bad = 0
foreach ($p in $ordered) {
    $path = PatchPath $p
    $applied = IsApplied $path
    # A later patch may change lines introduced by this patch, so reverse-check
    # alone is insufficient. Exact final hashes (also checked below) establish
    # the fully applied state without accepting untracked edits. Revert still
    # works strictly backwards, restoring each earlier patch's context.
    if (-not $applied -and -not $Revert) {
        $files = @($p.files.PSObject.Properties)
        $applied = $files.Count -gt 0
        foreach ($f in $files) {
            if ((FileHash $f.Name) -ne $f.Value) { $applied = $false; break }
        }
    }
    if ($Check) {
        $state = if ($applied) { 'applied' } else { 'NOT applied' }
        Write-Host ("{0}: {1}" -f $p.file, $state)
        if (-not $applied) { $bad++ }
        continue
    }
    if ($Revert) {
        if (-not $applied) { Write-Host "$($p.file): not applied, skipping"; continue }
        if ((GitSdk @('apply', '--reverse', $path)) -ne 0) { throw "$($p.file): reverse apply failed" }
        Write-Host "$($p.file): reverted"
        continue
    }
    if ($applied) { Write-Host "$($p.file): already applied"; continue }
    if ((GitSdk @('apply', '--check', $path)) -ne 0) {
        throw "$($p.file): does not apply to the pinned sources (local edits?)"
    }
    if ((GitSdk @('apply', $path)) -ne 0) { throw "$($p.file): apply failed" }
    Write-Host "$($p.file): applied"
}

if (-not $Revert) {
    foreach ($p in $patches) {
        foreach ($f in $p.files.PSObject.Properties) {
            $h = FileHash $f.Name
            if ($h -ne $f.Value) {
                Write-Host ("{0}: SHA-256 {1}, expected {2} after {3}" -f $f.Name, $h, $f.Value, $p.file)
                $bad++
            }
        }
    }
}
if ($bad) { exit 1 }
