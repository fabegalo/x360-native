# Invoked only after the GUI explains the system installation and obtains consent.
# The Microsoft installer owns elevation, license acceptance and component progress.
param([switch]$AllowInstallation, [switch]$DownloadOnly)
$ErrorActionPreference = 'Stop'
# Do not inherit incompatible module versions from a parent PowerShell 7.
$env:PSModulePath = Join-Path $PSHOME 'Modules'
if (-not ($AllowInstallation -or $DownloadOnly)) { throw 'Explicit installation/download consent is required.' }
$root = (Resolve-Path (Split-Path -Parent $PSScriptRoot)).Path
$cache = Join-Path $root '.local/downloads'
New-Item -ItemType Directory -Force $cache | Out-Null
$installer = Join-Path $cache 'vs_buildtools_2022.exe'
if (-not (Test-Path -LiteralPath $installer)) {
    Write-Output 'X360N_STAGE download Microsoft Build Tools 2022 bootstrapper'
    & "$env:SystemRoot\System32\curl.exe" --fail --location --retry 3 --output "$installer.part" 'https://aka.ms/vs/17/release/vs_buildtools.exe'
    if ($LASTEXITCODE -ne 0) { throw 'Microsoft installer download failed. Retry without changing Windows security settings.' }
    Move-Item -LiteralPath "$installer.part" -Destination $installer
}
$signature = Get-AuthenticodeSignature -LiteralPath $installer
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch '(^|,\s*)O=Microsoft Corporation(,|$)') {
    throw 'Installer signature is not valid Microsoft code. The file was NOT executed.'
}
$version = [Diagnostics.FileVersionInfo]::GetVersionInfo($installer)
if ($version.ProductVersion -notmatch '2022|^17\.') { throw "Unexpected installer product version: $($version.ProductVersion)" }
Write-Output "Verified Microsoft signature. Product: $($version.ProductVersion)"
Get-FileHash -LiteralPath $installer -Algorithm SHA256 | Format-List | Out-Host
if ($DownloadOnly) { exit 0 }
Write-Output 'X360N_STAGE waiting for Microsoft installer / aguardando instalador Microsoft'
# Visible interactive tool intentionally requested by the user in the consent dialog.
# No --quiet, no automatic reboot, no force-close of other Visual Studio sessions.
$process = Start-Process -FilePath $installer -ArgumentList @('--wait', '--add', 'Microsoft.VisualStudio.Workload.VCTools', '--includeRecommended') -WindowStyle Normal -PassThru -Wait
if ($process.ExitCode -notin @(0,3010)) { throw "Microsoft installer ended with $($process.ExitCode). Existing content and saves were preserved." }
Write-Output "Microsoft installer exit $($process.ExitCode). Recheck dependencies before preparing."
