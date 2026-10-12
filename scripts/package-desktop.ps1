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
$internal = Join-Path $destination 'app'
New-Item -ItemType Directory $internal | Out-Null
$payload = Join-Path $internal 'workspace'
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
Copy-Item -LiteralPath (Join-Path $binaries 'x360-native.exe') -Destination $destination
Copy-Item -LiteralPath (Join-Path $binaries 'x360n.exe') -Destination $internal
# Desktop-only Release statically links the compiler runtime; opening the UI
# requires neither developer tools nor a redistributable installer.
$cache = Get-Content -LiteralPath (Join-Path $root '.local/build/win-amd64/CMakeCache.txt')
if (-not ($cache -contains 'X360_DESKTOP_ONLY:BOOL=ON')) { throw 'Use the isolated desktop-only configuration for distribution.' }
$sdk = (($cache | Where-Object { $_ -like 'X360_REXGLUE_DIR:PATH=*' }) -replace '^X360_REXGLUE_DIR:PATH=', '')
$licenses = Join-Path $internal 'licenses'; New-Item -ItemType Directory $licenses | Out-Null
foreach ($component in @('fmt','cli11','tomlplusplus')) {
    Copy-Item -LiteralPath (Join-Path $sdk "thirdparty/$component/LICENSE") -Destination (Join-Path $licenses "$component.txt")
}
[IO.File]::WriteAllText((Join-Path $destination 'READ-ME.txt'), @"
x360-native $Version — prévia para Windows x64

Abra x360-native.exe. Adicione seus arquivos ou pastas originais na janela.
O aplicativo identifica as versões e mostra os componentes que ainda faltam.
Dashboards e jogos são gerenciados separadamente. Inicie os jogos pela My Games
original quando houver uma dashboard e uma preparação compatíveis disponíveis.

A pasta app contém os componentes internos. A pasta data será criada ao abrir
para guardar conteúdo, preparações e configurações. Não apague data para atualizar
ou remover apenas o aplicativo. Saves e perfis existentes são preservados.

Nenhum jogo, dashboard, módulo original, fonte ou recompilação proprietária é
incluído. Nenhum arquivo é enviado. A preparação de títulos oferece as ferramentas
verificadas com consentimento; a instalação dos componentes Microsoft pode exigir
permissão de administrador. Abrir a interface e importar arquivos não exige isso.

Esta prévia não comprova compatibilidade com todos os títulos nem instalação em
Windows limpo. Perfis, Guide e save/load ainda têm pendências.
Código correspondente: $head
SDK e licenças: https://github.com/rexglue/rexglue-sdk
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
