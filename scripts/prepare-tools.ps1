# Local, pinned preparation. No elevation, global PATH changes or package manager.
# Downloads are refused without explicit GUI/CLI consent; cached archives are reused.
param([switch]$AllowDownloads, [switch]$ToolsOnly, [switch]$UpdateDependencies)
$ErrorActionPreference = 'Stop'
# Use this host's built-in modules even when launched from PowerShell 7.
# An inherited PSModulePath can hide Windows PowerShell's Get-FileHash.
$env:PSModulePath = Join-Path $PSHOME 'Modules'
$root = (Resolve-Path (Split-Path -Parent $PSScriptRoot)).Path
$tools = Join-Path $root '.local/toolchains'
$cache = Join-Path $root '.local/cache/downloads'
$lock = Get-Content -Raw (Join-Path $PSScriptRoot 'setup/desktop-tools.lock.json') | ConvertFrom-Json
New-Item -ItemType Directory -Force $tools, $cache | Out-Null
try { $lease = [IO.File]::Open((Join-Path $root '.local/environment.lock'), [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None) }
catch { throw 'Environment in use. Close the platform or other preparation and retry.' }
try {
if (Test-Path -LiteralPath (Join-Path $root '.local/payload-update/pending')) { throw 'Atualizacao interrompida. Reabra o aplicativo antes de preparar.' }
function Stage([string]$message) {
    if ($env:X360N_PREPARATION_METRICS) {
        try {
            if (-not ('X360NPreparationProcess' -as [type])) { Add-Type -Path (Join-Path $PSScriptRoot 'build/preparation-process.cs') }
            $sample = [X360NPreparationProcess]::CurrentJob()
            $row = [ordered]@{schema=1;kind='setup-cumulative';stage=$message;
                monotonic_seconds=([Diagnostics.Stopwatch]::GetTimestamp()/[double][Diagnostics.Stopwatch]::Frequency);metrics=$sample}
            [IO.File]::AppendAllText($env:X360N_PREPARATION_METRICS,(($row | ConvertTo-Json -Depth 5 -Compress)+"`n"),[Text.UTF8Encoding]::new($false))
        } catch { Write-Warning "Setup metrics unavailable: $($_.Exception.Message)" }
    }
    Write-Output "X360N_STAGE $message"
}
function PreserveDependency([string]$Directory) {
    if (-not $UpdateDependencies) { throw 'Esta instalacao precisa atualizar suas ferramentas. Confirme Preparar no aplicativo; seus jogos e dados serao preservados.' }
    $resolved = [IO.Path]::GetFullPath($Directory)
    if (-not $resolved.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Dependency outside environment' }
    [IO.File]::WriteAllText((Join-Path $root '.local/dependency-update.pending'), 'incomplete')
    $backup = $resolved + '.previous-' + [guid]::NewGuid().ToString('N')
    Move-Item -LiteralPath $resolved -Destination $backup
    Stage "preserved previous dependency: $backup"
}
. (Join-Path $PSScriptRoot 'setup/download-tools.ps1')
$pendingTools = @($lock.tools | Where-Object {
    $destination = Join-Path $tools $_.directory
    $receipt = Join-Path $destination '.x360n-archive-sha256'
    -not ((Test-Path -LiteralPath (Join-Path $destination $_.entry)) -and
        (Test-Path -LiteralPath $receipt) -and (Get-Content -Raw -LiteralPath $receipt).Trim() -eq $_.sha256)
})
foreach ($tool in $pendingTools) {
    if ((Test-Path -LiteralPath (Join-Path $tools $tool.directory)) -and -not $UpdateDependencies) {
        throw 'Ferramenta incompleta ou alterada. Confirme a atualizacao em Preparar; os arquivos anteriores serao preservados.'
    }
}
if (-not $ToolsOnly) {
    $llvm = (Get-Content -Raw (Join-Path $root 'deps.lock.json') | ConvertFrom-Json).llvm
    $llvmDirectory = Join-Path $root $llvm.install_dir
    if (-not (Test-Path -LiteralPath (Join-Path $llvmDirectory 'bin/clang++.exe'))) {
        if (Test-Path -LiteralPath $llvmDirectory) {
            if (-not $UpdateDependencies) { throw 'LLVM incompleto. Confirme a atualizacao em Preparar; os arquivos anteriores serao preservados.' }
            PreserveDependency $llvmDirectory
        }
        # Download the large compiler archive alongside independent tools. The
        # existing LLVM bootstrap still verifies SHA and atomically extracts it.
        $pendingTools += [pscustomobject]@{id='llvm';asset=$llvm.asset;url=$llvm.url;sha256=$llvm.sha256;size=0}
    }
}
$downloads = Get-VerifiedToolArchives $pendingTools $cache -AllowDownloads:$AllowDownloads
try {
foreach ($tool in $lock.tools) {
    $destination = Join-Path $tools $tool.directory
    $receipt = Join-Path $destination '.x360n-archive-sha256'
    if ((Test-Path -LiteralPath (Join-Path $destination $tool.entry)) -and
        (Test-Path -LiteralPath $receipt) -and (Get-Content -Raw -LiteralPath $receipt).Trim() -eq $tool.sha256) {
        Stage "reuse $($tool.id) $($tool.version)"
        continue
    }
    if ((Test-Path -LiteralPath $destination) -and -not $UpdateDependencies) { throw 'Ferramenta incompleta ou alterada. Confirme a atualizacao em Preparar; os arquivos anteriores serao preservados.' }
    $archive = $downloads.archives[$tool.id]
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
    if (Test-Path -LiteralPath $destination) { PreserveDependency $destination }
    Move-Item -LiteralPath $payload -Destination $destination
    if ($payload -ne $staging) { Remove-Item -LiteralPath $staging } # Empty directory only.
}
} finally { foreach ($handle in $downloads.leases) { $handle.Dispose() } }
$git = Join-Path $tools 'mingit-2.56.0.2/cmd/git.exe'
$env:PATH = (Split-Path $git) + ';' + $env:PATH
if ($ToolsOnly) { Stage 'tools-ready'; exit 0 } # Cannot certify SDK/LLVM or clear a pending full update.
$sdkLock = (Get-Content -Raw (Join-Path $root 'deps.lock.json') | ConvertFrom-Json).rexglue_sdk
$sdk = Join-Path $root 'external/rexglue-sdk'
if (Test-Path -LiteralPath $sdk) {
    $sdkMatches = $false
    if (Test-Path -LiteralPath (Join-Path $sdk '.git')) {
        $currentHead = & $git -C $sdk rev-parse HEAD
        $sdkMatches = $LASTEXITCODE -eq 0 -and $currentHead.Trim() -eq $sdkLock.commit
        if ($sdkMatches) {
            $sdkState = & $git -C $sdk submodule status --recursive
            if ($LASTEXITCODE -ne 0 -or ($sdkState | Where-Object { $_ -match '^[+U]' })) { $sdkMatches = $false }
        }
        # An old patch set cannot be overlaid on an incompatible patched tree.
        # Pristine or already matching files are safe for the existing applicator.
        if ($sdkMatches) {
            $changed = @(& $git -C $sdk diff --name-only HEAD)
            foreach ($patch in $sdkLock.patches) { foreach ($file in $patch.files.PSObject.Properties) {
                if ($changed -contains $file.Name) {
                    $path = Join-Path $sdk $file.Name
                    if (-not (Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.Value) { $sdkMatches = $false }
                }
            } }
        }
    }
    if (-not $sdkMatches) {
        if (-not $AllowDownloads) { throw 'O SDK precisa ser atualizado. Autorize os downloads em Preparar; a versao anterior e seus dados serao preservados.' }
        PreserveDependency $sdk
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $sdk '.git'))) {
    if (Test-Path -LiteralPath $sdk) { throw "Unrecognized SDK directory, preserved: $sdk" }
    if (-not $AllowDownloads) { throw "Download consent required: ReXGlue SDK and its pinned submodules." }
    Stage 'download SDK'
    $stageSdk = Join-Path $root ('external/.sdk-' + [guid]::NewGuid().ToString('N'))
    & $git -c core.longpaths=true clone --no-checkout $sdkLock.url $stageSdk
    if ($LASTEXITCODE -ne 0) { throw 'SDK clone failed; retry preparation.' }
    & $git -c core.longpaths=true -C $stageSdk checkout --detach $sdkLock.commit
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
    & $git -c core.longpaths=true -C $sdk submodule update --init --recursive --jobs 8
    if ($LASTEXITCODE -ne 0) { throw 'SDK dependency download failed; retry preparation.' }
}
Stage 'verify SDK patches'
& (Join-Path $PSScriptRoot 'setup/materialize-symlinks.ps1')
& (Join-Path $PSScriptRoot 'setup/apply-sdk-patches.ps1')
if ($LASTEXITCODE -ne 0) { throw 'SDK patch verification failed.' }
Stage 'prepare LLVM'
& (Join-Path $PSScriptRoot 'setup/bootstrap-llvm.ps1') -AllowDownloads:$AllowDownloads
if ($LASTEXITCODE -ne 0) { throw 'LLVM preparation failed.' }
$pending = Join-Path $root '.local/dependency-update.pending'
if (Test-Path -LiteralPath $pending) { Remove-Item -LiteralPath $pending }
Stage 'tools-ready'
} finally { $lease.Dispose() }
