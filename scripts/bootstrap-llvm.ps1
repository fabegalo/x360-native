# Downloads the LLVM release pinned in deps.lock.json into .local/toolchains.
# No admin rights, no global PATH or registry changes. Idempotent.
# Usage: powershell -ExecutionPolicy Bypass -File scripts/bootstrap-llvm.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$lock = (Get-Content -Raw (Join-Path $root 'deps.lock.json') | ConvertFrom-Json).llvm

$installDir = Join-Path $root $lock.install_dir
$clang = Join-Path $installDir 'bin\clang++.exe'
if (Test-Path $clang) {
    Write-Host "LLVM already present: $installDir"
    & $clang --version
    exit 0
}

$dlDir = Join-Path $root '.local\downloads'
New-Item -ItemType Directory -Force $dlDir | Out-Null
$archive = Join-Path $dlDir $lock.asset

if (-not (Test-Path $archive)) {
    Write-Host "Downloading $($lock.url)"
    & curl.exe -L --fail --retry 3 -o "$archive.part" $lock.url
    if ($LASTEXITCODE -ne 0) { throw "curl failed with exit code $LASTEXITCODE" }
    Move-Item "$archive.part" $archive
}

$actual = (Get-FileHash -Algorithm SHA256 $archive).Hash.ToLowerInvariant()
if ($actual -ne $lock.sha256) {
    throw "SHA-256 mismatch for $archive`n  expected $($lock.sha256)`n  actual   $actual"
}
Write-Host "SHA-256 verified: $actual"

# Extract into a staging dir, then rename, so a partial extract never looks installed.
$staging = "$installDir.staging"
if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
New-Item -ItemType Directory -Force $staging | Out-Null
& "$env:SystemRoot\System32\tar.exe" -xf $archive -C $staging --strip-components 1
if ($LASTEXITCODE -ne 0) { throw "tar failed with exit code $LASTEXITCODE" }
Move-Item $staging $installDir

& $clang --version
if ($LASTEXITCODE -ne 0) { throw "clang++ --version failed" }
