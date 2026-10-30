# Downloads the LLVM release pinned in deps.lock.json into .local/toolchains.
# No admin rights, no global PATH or registry changes. Idempotent.
# Usage: powershell -ExecutionPolicy Bypass -File scripts/setup/bootstrap-llvm.ps1
param([switch]$AllowDownloads, [ValidateSet('tar','7zip')][string]$Extractor = 'tar')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $PSScriptRoot 'host-platform.ps1')
# The native LLVM of this machine (x64 or ARM64).
$lock = Get-LockedWindowsLlvm (Get-Content -Raw (Join-Path $root 'deps.lock.json') | ConvertFrom-Json) (Get-HostPlatform)

$installDir = Join-Path $root $lock.install_dir
$clang = Join-Path $installDir 'bin\clang++.exe'
if (Test-Path $clang) {
    Write-Host "LLVM already present: $installDir"
    & $clang --version
    exit 0
}

$dlDir = Join-Path $root '.local\cache\downloads'
New-Item -ItemType Directory -Force $dlDir | Out-Null
$archive = Join-Path $dlDir $lock.asset

if (-not (Test-Path $archive)) {
    if (-not $AllowDownloads) { throw "Download consent required: $($lock.url). Run with -AllowDownloads or use the preparation app." }
    Write-Host "Downloading $($lock.url)"
    # Resume an interrupted transfer instead of discarding hundreds of MiB.
    # Renew stalled connections; only the locked full-file hash permits use.
    for ($attempt = 1; $attempt -le 4; $attempt++) {
        & "$env:SystemRoot\System32\curl.exe" -L --fail --continue-at - --connect-timeout 30 --speed-limit 262144 --speed-time 30 -o "$archive.part" $lock.url
        if ($LASTEXITCODE -eq 0) { break }
        if ($LASTEXITCODE -notin @(18,28,52,55,56) -or $attempt -eq 4) {
            throw "LLVM download interrupted (curl $LASTEXITCODE). Partial bytes preserved; retry preparation."
        }
        Write-Host "X360N_STAGE resume LLVM download (attempt $($attempt + 1))"
    }
    Move-Item "$archive.part" $archive
}

$actual = (Get-FileHash -Algorithm SHA256 $archive).Hash.ToLowerInvariant()
if ($actual -ne $lock.sha256) {
    throw "SHA-256 mismatch for $archive`n  expected $($lock.sha256)`n  actual   $actual"
}
Write-Host "SHA-256 verified: $actual"

# Extract into a staging dir, then rename, so a partial extract never looks installed.
$staging = "$installDir.staging-$([guid]::NewGuid().ToString('N'))"
New-Item -ItemType Directory -Force $staging | Out-Null
$tarInput = $archive
$tarDirectory = $null
if ($Extractor -eq '7zip') {
    $sevenZip = Join-Path $env:ProgramFiles '7-Zip/7z.exe'
    if (-not (Test-Path -LiteralPath $sevenZip -PathType Leaf)) { throw '7-Zip missing on hosted runner; cannot extract the locked LLVM archive.' }
    # Windows Server's libarchive XZ decoder stalled for >15 minutes on the
    # hosted image. Decode the verified XZ with 7-Zip, then reuse tar's existing
    # strip-components/layout handling. No binary PowerShell pipeline.
    $tarDirectory = Join-Path $dlDir ('llvm-tar-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tarDirectory | Out-Null
    Write-Host 'X360N_STAGE decode verified LLVM XZ with hosted 7-Zip'
    & $sevenZip x $archive "-o$tarDirectory" -y -bsp0
    if ($LASTEXITCODE -ne 0) { throw '7-Zip LLVM XZ decoding failed' }
    $tarInput = Join-Path $tarDirectory ([IO.Path]::GetFileNameWithoutExtension($archive))
    if (-not (Test-Path -LiteralPath $tarInput -PathType Leaf)) { throw 'Decoded LLVM TAR missing' }
    if ($env:GITHUB_ACTIONS -eq 'true') {
        $evidence = [ordered]@{ name = '7zip'; sha256 = (Get-FileHash -LiteralPath $sevenZip -Algorithm SHA256).Hash.ToLowerInvariant() }
        $evidenceDirectory = Join-Path $root '.local/release'
        New-Item -ItemType Directory -Force $evidenceDirectory | Out-Null
        [IO.File]::WriteAllText((Join-Path $evidenceDirectory 'llvm-extractor.json'), ($evidence | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
    }
}
Write-Host 'X360N_STAGE extract LLVM TAR into staging'
& "$env:SystemRoot\System32\tar.exe" -xf $tarInput -C $staging --strip-components 1
if ($LASTEXITCODE -ne 0) { throw "tar failed with exit code $LASTEXITCODE" }
if ($tarDirectory) {
    # Only the uniquely named decoded TAR and its now-empty directory.
    Remove-Item -LiteralPath $tarInput
    Remove-Item -LiteralPath $tarDirectory
}
Move-Item $staging $installDir

& $clang --version
if ($LASTEXITCODE -ne 0) { throw "clang++ --version failed" }
