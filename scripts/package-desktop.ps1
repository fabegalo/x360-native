# Build the local Windows candidate from committed, audited source only.
# Does not publish a release or include any local game, SDK checkout or AOT output.
param([string]$Version = '0.1.0-preview', [string]$OutputDirectory = '.local/packages')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Split-Path -Parent $PSScriptRoot)).Path
if ($Version -notmatch '^[a-zA-Z0-9._-]+$') { throw 'Invalid package version' }
$base = [IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
if (-not $base.StartsWith((Join-Path $root '.local') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Packages must remain below .local' }
$name = "x360-native-$Version-windows-x64"
$destination = Join-Path $base $name
if (Test-Path -LiteralPath $destination) { throw "Package already exists; use a new version: $destination" }
$head = (git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify committed source' }
git -C $root diff --quiet HEAD -- src/desktop src/supervisor scripts/package-desktop.ps1 scripts/prepare-tools.ps1 scripts/desktop-tools.lock.json
if ($LASTEXITCODE -ne 0) { throw 'Commit the candidate implementation before packaging; binaries and exported source must match.' }
# Build only the redistributable frontend/supervisor, not proprietary titles.
& (Join-Path $PSScriptRoot 'devshell.cmd') cmake --build --preset win-amd64-release --target x360-native
if ($LASTEXITCODE -ne 0) { throw 'Release frontend build failed' }
New-Item -ItemType Directory -Force $destination | Out-Null
$payload = Join-Path $destination 'workspace'
New-Item -ItemType Directory $payload | Out-Null
$sourceArchive = Join-Path $destination 'source.zip'
git -C $root archive --format=zip "--output=$sourceArchive" HEAD
if ($LASTEXITCODE -ne 0) { throw 'Source export failed' }
Expand-Archive -LiteralPath $sourceArchive -DestinationPath $payload
Remove-Item -LiteralPath $sourceArchive
$index = [Collections.Generic.List[string]]::new()
foreach ($file in (Get-ChildItem -LiteralPath $payload -Recurse -File | Sort-Object FullName)) {
    $rel = $file.FullName.Substring($payload.Length + 1).Replace('\', '/')
    if ($rel -match '(^|/)(\.local|\.git|external)/' -or $rel -match '\.(xex|xexp|xtt|ttf|otf|pdb|dll|exe|stfs|iso)$' -or
        ($rel -match '/generated/' -and $rel -notmatch '/generated/rexglue\.cmake$')) { throw "Non-distributable or unreviewed payload entry: $rel" }
    $index.Add((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + "`t" + $rel)
}
[IO.File]::WriteAllLines((Join-Path $payload 'payload.sha256'), $index, [Text.UTF8Encoding]::new($false))
$binaries = Join-Path $root '.local/build/win-amd64/src/desktop/Release'
foreach ($file in @('x360-native.exe', 'x360n.exe')) { Copy-Item -LiteralPath (Join-Path $binaries $file) -Destination $destination }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = (& $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
$redist = Get-ChildItem -LiteralPath (Join-Path $vs 'VC/Redist/MSVC') -Directory | Where-Object Name -Match '^14\.' | Sort-Object Name -Descending | Select-Object -First 1
$crt = Join-Path $redist.FullName 'x64/Microsoft.VC143.CRT'
foreach ($file in @('msvcp140.dll','msvcp140_1.dll','msvcp140_2.dll','msvcp140_atomic_wait.dll','vcruntime140.dll','vcruntime140_1.dll')) {
    Copy-Item -LiteralPath (Join-Path $crt $file) -Destination $destination
}
$licenses = Join-Path $destination 'licenses'; New-Item -ItemType Directory $licenses | Out-Null
foreach ($component in @('fmt','cli11','tomlplusplus')) {
    Copy-Item -LiteralPath (Join-Path $root "external/rexglue-sdk/thirdparty/$component/LICENSE") -Destination (Join-Path $licenses "$component.txt")
}
[IO.File]::WriteAllText((Join-Path $destination 'READ-ME.txt'), @"
x360-native $Version — Windows x64 preview

Open x360-native.exe. Choose a separate folder for your environment and data.
Import your authorized extracted Retail 17559 set and Castle Crashers 0.0.1.3.
Prepare, then open the original dashboard. Launch games through My Games.
No dashboard, game, font, XAM, boot animation or derived native title is included.
Tools are verified and obtained locally after consent. MSVC 2022 C++ / Windows SDK
is required; the app offers the verified official installer when it is missing.
Its system installation can require administrator consent; the regular app does not.
Profiles and save/load acceptance are NOT complete. This is a test candidate,
not a release approved for end users. No clean-machine acceptance is claimed.

To remove the app, delete only this package folder. Keep the environment folder
to preserve imported content, builds, profiles and saves. No uninstaller deletes it.
No telemetry or automatic uploads. Review logs locally before sharing them.
SDK sources and licenses: https://github.com/rexglue/rexglue-sdk
Microsoft runtime distribution: https://aka.ms/vs/17/redist.txt
Private project source snapshot: $head
"@, [Text.UTF8Encoding]::new($false))
$metadata = @{ schema = 1; version = $Version; platform = 'windows-x64'; commit = $head; acceptance = 'pending'; files = @() }
foreach ($file in (Get-ChildItem -LiteralPath $destination -Recurse -File | Sort-Object FullName)) {
    $metadata.files += @{ path = $file.FullName.Substring($destination.Length + 1).Replace('\','/'); size = $file.Length; sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
}
[IO.File]::WriteAllText((Join-Path $destination 'package.json'), ($metadata | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
$zip = Join-Path $base "$name.zip"
Compress-Archive -LiteralPath $destination -DestinationPath $zip
Get-FileHash -LiteralPath $zip -Algorithm SHA256
Write-Output "Local candidate: $zip"
