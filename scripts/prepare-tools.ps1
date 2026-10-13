# Local, pinned preparation. No elevation, global PATH changes or package manager.
# Downloads are refused without explicit GUI/CLI consent; cached archives are reused.
param([switch]$AllowDownloads, [switch]$ToolsOnly)
$ErrorActionPreference = 'Stop'
# Use this host's built-in modules even when launched from PowerShell 7.
# An inherited PSModulePath can hide Windows PowerShell's Get-FileHash.
$env:PSModulePath = Join-Path $PSHOME 'Modules'
$root = (Resolve-Path (Split-Path -Parent $PSScriptRoot)).Path
$tools = Join-Path $root '.local/toolchains'
$cache = Join-Path $root '.local/downloads'
$lock = Get-Content -Raw (Join-Path $PSScriptRoot 'desktop-tools.lock.json') | ConvertFrom-Json
New-Item -ItemType Directory -Force $tools, $cache | Out-Null
try { $lease = [IO.File]::Open((Join-Path $root '.local/environment.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None) }
catch { throw 'Environment in use. Close the platform or other preparation and retry.' }
try {
function Stage([string]$message) { Write-Output "X360N_STAGE $message" }
foreach ($tool in $lock.tools) {
    $destination = Join-Path $tools $tool.directory
    $receipt = Join-Path $destination '.x360n-archive-sha256'
    if ((Test-Path -LiteralPath (Join-Path $destination $tool.entry)) -and
        (Test-Path -LiteralPath $receipt) -and (Get-Content -Raw -LiteralPath $receipt).Trim() -eq $tool.sha256) {
        Stage "reuse $($tool.id) $($tool.version)"
        continue
    }
    if (Test-Path -LiteralPath $destination) { throw "Incomplete or changed tool: $destination. Preserve it and choose a new environment." }
    $archive = Join-Path $cache $tool.asset
    if (-not (Test-Path -LiteralPath $archive)) {
        if (-not $AllowDownloads) { throw "Download consent required: $($tool.url) ($($tool.size) bytes)" }
        Stage "download $($tool.id) $($tool.size) bytes"
        & "$env:SystemRoot\System32\curl.exe" --fail --location --retry 3 --output "$archive.part" $tool.url
        if ($LASTEXITCODE -ne 0) { throw "Download failed ($LASTEXITCODE): $($tool.url). Retry to resume preparation." }
        if ((Get-FileHash -LiteralPath "$archive.part" -Algorithm SHA256).Hash.ToLowerInvariant() -ne $tool.sha256) { throw "Downloaded archive checksum mismatch: $($tool.asset)" }
        Move-Item -LiteralPath "$archive.part" -Destination $archive
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $tool.sha256) { throw "Cached archive checksum mismatch: $archive" }
    Stage "extract $($tool.id)"
    # A unique staging directory makes interruption retryable without deleting anything.
    $staging = Join-Path $tools ('.staging-' + $tool.id + '-' + [guid]::NewGuid().ToString('N'))
    # Windows PowerShell's ZIP module fails at MAX_PATH inside a normal
    # extracted package directory. Use the Windows tar already used for LLVM,
    # removing the release's wrapper directory during extraction.
    New-Item -ItemType Directory -Path $staging | Out-Null
    $extractArgs = @('-xf', $archive, '-C', $staging)
    if ($tool.prefix) { $extractArgs += @('--strip-components', '1') }
    & "$env:SystemRoot\System32\tar.exe" @extractArgs
    if ($LASTEXITCODE -ne 0) { throw "Tool archive extraction failed: $($tool.asset). Retry preparation." }
    $payload = $staging
    if (-not (Test-Path -LiteralPath (Join-Path $payload $tool.entry))) { throw "Archive entry missing: $($tool.entry)" }
    [IO.File]::WriteAllText((Join-Path $payload '.x360n-archive-sha256'), $tool.sha256)
    Move-Item -LiteralPath $payload -Destination $destination
    if ($payload -ne $staging) { Remove-Item -LiteralPath $staging } # Empty directory only.
}
$git = Join-Path $tools 'mingit-2.56.0.2/cmd/git.exe'
$env:PATH = (Split-Path $git) + ';' + $env:PATH
if ($ToolsOnly) { Stage 'tools-ready'; exit 0 }
$sdkLock = (Get-Content -Raw (Join-Path $root 'deps.lock.json') | ConvertFrom-Json).rexglue_sdk
$sdk = Join-Path $root 'external/rexglue-sdk'
if (-not (Test-Path -LiteralPath (Join-Path $sdk '.git'))) {
    if (Test-Path -LiteralPath $sdk) { throw "Unrecognized SDK directory, preserved: $sdk" }
    if (-not $AllowDownloads) { throw "Download consent required: ReXGlue SDK and its pinned submodules." }
    Stage 'download SDK'
    $stageSdk = Join-Path $root ('external/.sdk-' + [guid]::NewGuid().ToString('N'))
    & $git clone --no-checkout $sdkLock.url $stageSdk
    if ($LASTEXITCODE -ne 0) { throw 'SDK clone failed; retry preparation.' }
    & $git -C $stageSdk checkout --detach $sdkLock.commit
    if ($LASTEXITCODE -ne 0) { throw 'Pinned SDK revision unavailable.' }
    Move-Item -LiteralPath $stageSdk -Destination $sdk
}
$head = (& $git -C $sdk rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $head -ne $sdkLock.commit) { throw "SDK revision differs from the lock; preserved: $sdk" }
$submodules = & $git -C $sdk submodule status --recursive
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect SDK dependencies.' }
if ($submodules | Where-Object { $_ -match '^[+U]' }) { throw 'SDK submodule revision differs; preserve the checkout and select a clean environment.' }
if ($submodules | Where-Object { $_ -match '^-' }) {
    if (-not $AllowDownloads) { throw 'Download consent required for missing SDK dependencies.' }
    Stage 'download SDK dependencies'
    & $git -C $sdk submodule update --init --recursive --jobs 8
    if ($LASTEXITCODE -ne 0) { throw 'SDK dependency download failed; retry preparation.' }
}
Stage 'verify SDK patches'
& (Join-Path $PSScriptRoot 'materialize-symlinks.ps1')
& (Join-Path $PSScriptRoot 'apply-sdk-patches.ps1')
if ($LASTEXITCODE -ne 0) { throw 'SDK patch verification failed.' }
Stage 'prepare LLVM'
& (Join-Path $PSScriptRoot 'bootstrap-llvm.ps1') -AllowDownloads:$AllowDownloads
if ($LASTEXITCODE -ne 0) { throw 'LLVM preparation failed.' }
Stage 'tools-ready'
} finally { $lease.Dispose() }
