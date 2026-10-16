# Build from committed source in a fresh distribution-only tree. Never publishes.
param([string]$Version = '0.1.0-preview', [string]$OutputDirectory = '.local/packages',
      [ValidateSet('local-candidate','public-prerelease')][string]$Publication = 'local-candidate',
      [string]$ProvenanceFile = '')
$ErrorActionPreference = 'Stop'
$env:PSModulePath = Join-Path $PSHOME 'Modules'
$root = (Resolve-Path (Split-Path -Parent $PSScriptRoot)).Path
if ($Version -notmatch '^[a-zA-Z0-9._-]+$') { throw 'Invalid package version' }
$base = [IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
if (-not $base.StartsWith((Join-Path $root '.local') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Packages must remain below .local' }
$name = "x360-native-$Version-windows-x64"
$destination = Join-Path $base $name
if ((Test-Path -LiteralPath $destination) -or (Test-Path -LiteralPath "$destination.zip")) { throw 'Package already exists; choose another output directory.' }
$head = (git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify committed source' }
$provenance = $null
if ($ProvenanceFile) {
    $provenance = Get-Content -Raw -LiteralPath $ProvenanceFile | ConvertFrom-Json
    if ($provenance.commit -ne $head -or $provenance.version -ne $Version) { throw 'CI provenance differs from the committed package source/version' }
}
git -C $root diff --quiet HEAD -- scripts/package-desktop.ps1
if ($LASTEXITCODE -ne 0) { throw 'Commit the packaging script before using it.' }
$job = Join-Path $root ('.local/distribution/' + [guid]::NewGuid().ToString('N'))
$source = Join-Path $job 'source'
$build = Join-Path $job 'build'
New-Item -ItemType Directory -Force $source, $destination | Out-Null
function ExportTree([string]$Repository, [string]$Revision, [string]$Directory) {
    New-Item -ItemType Directory -Force $Directory | Out-Null
    $archive = Join-Path $job ([guid]::NewGuid().ToString('N') + '.zip')
    git -c core.autocrlf=false -c core.eol=lf -C $Repository archive --format=zip "--output=$archive" $Revision
    if ($LASTEXITCODE -ne 0) { throw "Cannot export committed dependency: $Repository $Revision" }
    & "$env:SystemRoot\System32\tar.exe" -xf $archive -C $Directory
    if ($LASTEXITCODE -ne 0) { throw 'Source snapshot extraction failed' }
    Remove-Item -LiteralPath $archive
}
function Hash([string]$File) { (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant() }
function WriteIndex([string]$Directory, [string]$Name) {
    $lines = @(Get-ChildItem -LiteralPath $Directory -Recurse -File | Sort-Object FullName | ForEach-Object {
        (Hash $_.FullName) + "`t" + $_.FullName.Substring($Directory.Length + 1).Replace('\','/')
    })
    [IO.File]::WriteAllText((Join-Path $Directory $Name), ($lines -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
}
ExportTree $root $head $source
$internal = Join-Path $destination 'app'
$payload = Join-Path $internal 'workspace'
New-Item -ItemType Directory -Force $payload | Out-Null
# Separate source export: build coordination files must never enter the payload.
ExportTree $root $head $payload
foreach ($file in Get-ChildItem -LiteralPath $payload -Recurse -File) {
    $rel = $file.FullName.Substring($payload.Length + 1).Replace('\','/')
    if ($rel -match '(^|/)(\.local|\.git|external)/' -or $rel -match '\.(xex|xexp|xtt|ttf|otf|pdb|dll|exe|stfs|iso)$' -or
        ($rel -match '/generated/' -and $rel -notmatch '/generated/rexglue\.cmake$')) { throw "Non-distributable payload entry: $rel" }
}
WriteIndex $payload 'payload.sha256'
$lock = Get-Content -Raw (Join-Path $source 'deps.lock.json') | ConvertFrom-Json
$sdk = Join-Path $source 'external/rexglue-sdk'
# A private checkout uses only pinned Git objects, never developer working bytes.
# Shared objects are build-time inputs only; no checkout or Git directory is shipped.
git -c core.longpaths=true clone --shared --no-checkout (Join-Path $root 'external/rexglue-sdk') $sdk
if ($LASTEXITCODE -ne 0) { throw 'SDK snapshot failed' }
git -c core.longpaths=true -c core.autocrlf=false -c core.eol=lf -C $sdk checkout --detach $lock.rexglue_sdk.commit
if ($LASTEXITCODE -ne 0) { throw 'Pinned SDK checkout failed' }
$dependencies = [ordered]@{}
foreach ($component in @('fmt','tomlplusplus','cli11','catch2','inja','sdl3','spdlog','simde','utfcpp')) {
    $tree = git -C $sdk ls-tree HEAD "thirdparty/$component"
    if ($LASTEXITCODE -ne 0 -or $tree -notmatch '^160000 commit ([a-f0-9]{40})') { throw "Unpinned dependency: $component" }
    $revision = $Matches[1]
    ExportTree (Join-Path $root "external/rexglue-sdk/thirdparty/$component") $revision (Join-Path $sdk "thirdparty/$component")
    $dependencies[$component] = $revision
}
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $source 'scripts/apply-sdk-patches.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Pinned SDK patch verification failed' }
$llvm = Join-Path $root $lock.llvm.install_dir
$clang = Join-Path $llvm 'bin/clang++.exe'
foreach ($tool in @($clang, (Join-Path $llvm 'bin/llvm-readobj.exe'))) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Locked LLVM dependency missing: $tool" }
}
$compilerVersion = (& $clang --version | Select-Object -First 1)
if ($compilerVersion -notlike "* $($lock.llvm.version)*") { throw 'Compiler differs from the locked LLVM version' }
$sourceMap = $source.Replace('\','/')
$payloadHash = Hash (Join-Path $payload 'payload.sha256')
$flags = "-march=x86-64-v2 -mno-incremental-linker-compatible -ffile-prefix-map=$sourceMap=/x360-source"
$devshell = Join-Path $root 'scripts/devshell.cmd'
& $devshell cmake -S $source -B $build -G Ninja -DX360_DESKTOP_ONLY=ON -DX360_DISTRIBUTION=ON "-DX360_PACKAGE_PAYLOAD_SHA256=$payloadHash" -DCMAKE_BUILD_TYPE=Release "-DCMAKE_C_COMPILER=$llvm/bin/clang.exe" "-DCMAKE_CXX_COMPILER=$clang" "-DCMAKE_C_FLAGS=$flags" "-DCMAKE_CXX_FLAGS=$flags" '-DCMAKE_EXE_LINKER_FLAGS=-Wl,/Brepro' -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
if ($LASTEXITCODE -ne 0) { throw 'Distribution configure failed' }
& $devshell cmake --build $build --target x360-native x360n_desktop_tests
if ($LASTEXITCODE -ne 0) { throw 'Distribution build failed' }
# This isolated tree registers only distribution tests, never SDK/game suites.
& $devshell ctest --test-dir $build -R desktop.catalog --no-tests=error --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Distribution tests failed; ZIP withheld' }
$binaries = Join-Path $build 'src/desktop'
Copy-Item -LiteralPath (Join-Path $binaries 'x360-native.exe') -Destination $destination
Copy-Item -LiteralPath (Join-Path $binaries 'x360n.exe') -Destination $internal
Copy-Item -LiteralPath (Join-Path $build 'src/content/x360n-extract.exe') -Destination $internal
# Static compiler runtime, explicit OS-only import allowlist. Never harvest DLLs.
foreach ($binary in @((Join-Path $destination 'x360-native.exe'), (Join-Path $internal 'x360n.exe'), (Join-Path $internal 'x360n-extract.exe'))) {
    $imports = & (Join-Path $llvm 'bin/llvm-readobj.exe') --coff-imports $binary
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect binary imports' }
    foreach ($line in $imports) {
        if ($line -match '^  Name: (.+)$' -and $Matches[1] -notmatch '^(KERNEL32|USER32|GDI32|ADVAPI32|SHELL32|ole32|OLEAUT32|COMDLG32|COMCTL32|bcrypt|dbghelp|d3d12|dxgi|XINPUT1_4|dwmapi|WS2_32|ntdll|VERSION|IMM32|SHLWAPI|gdiplus|uxtheme)\.dll$' -and $Matches[1] -notmatch '^api-ms-win-.*\.dll$') { throw "Unexpected runtime dependency: $($Matches[1])" }
    }
}
# Legal material is visible at the ZIP root, and comes from the same commit.
Copy-Item -LiteralPath (Join-Path $source 'licenses') -Destination $destination -Recurse
foreach ($notice in @('LICENSE','THIRD_PARTY_NOTICES.md')) {
    Copy-Item -LiteralPath (Join-Path $source $notice) -Destination $destination
}
# Actual compiler-discovered transitive SDK inputs, for the scope check below.
$ninjaInputs = & $devshell ninja -C $build -t deps
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect compiler dependency evidence' }
$sdkInputs = @($ninjaInputs | ForEach-Object {
    $line = $_.Trim().Replace('\','/')
    if ($line -match '/external/rexglue-sdk/([^:]+)$') { $Matches[1] }
} | Sort-Object -Unique)
if (-not $sdkInputs.Count) { throw 'Compiler dependency evidence is empty' }
[IO.File]::WriteAllText((Join-Path $internal 'dependency-inputs.json'), (ConvertTo-Json -InputObject $sdkInputs), [Text.UTF8Encoding]::new($false))
$links = & $devshell ninja -C $build -t query src/desktop/x360-native.exe src/supervisor/x360n.exe src/content/x360n-extract.exe
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect linker dependency evidence' }
$libraries = @($links | ForEach-Object {
    if ($_ -match '^\s+\|\s+(.+\.(?:lib|a))$') { $Matches[1].Replace('\','/') }
} | Sort-Object -Unique)
[IO.File]::WriteAllText((Join-Path $internal 'link-inputs.json'), (ConvertTo-Json -InputObject $libraries), [Text.UTF8Encoding]::new($false))
# Keep the effective compiler commands as reviewable evidence, with portable
# logical roots. They describe this fresh build, not a second preparation cache.
$commands = Get-Content -Raw -LiteralPath (Join-Path $build 'compile_commands.json') | ConvertFrom-Json
$rcCompiler = ((Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt') | Where-Object { $_ -match '^CMAKE_RC_COMPILER:FILEPATH=' }) -replace '^CMAKE_RC_COMPILER:FILEPATH=', '')
$rcCommand = $commands | Where-Object { $_.file -like '*.rc' } | Select-Object -First 1
if ($rcCommand.command -notmatch '^\s*(?:"([^"]+)"|(\S+))') { throw 'Resource compiler command missing' }
$rcActual = if ($Matches[1]) { $Matches[1] } else { $Matches[2] }
if ((Hash $rcActual) -ne (Hash $rcCompiler)) { throw 'Resource compiler differs from configuration' }
$portableCommands = @($commands | ForEach-Object {
    $entry = $_ | ConvertTo-Json -Compress
    foreach ($mapping in @(@($source, '<source>'), @($build, '<build>'), @($llvm, '<llvm>'), @($rcCompiler, '<windows-sdk>/rc.exe'), @($rcActual, '<windows-sdk>/rc.exe'))) {
        $entry = $entry.Replace($mapping[0].Replace('\','/'), $mapping[1]).Replace($mapping[0].Replace('/','\').Replace('\','\\'), $mapping[1])
    }
    if ($entry -match '(?i)[a-z]:[/\\]') { throw 'Absolute developer path in distribution build evidence' }
    $entry | ConvertFrom-Json
})
[IO.File]::WriteAllText((Join-Path $internal 'compile-commands.json'), ($portableCommands | ConvertTo-Json -Depth 5), [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $destination 'READ-ME.txt'), @"
x360-native $Version — prévia para Windows x64

Extraia o ZIP inteiro em uma pasta vazia e abra x360-native.exe.
Em Importar > Adicionar arquivo, selecione uma ISO ou um pacote Xbox 360
(CON/LIVE/PIRS, incluindo XBLA). A extração é local e não requer instalar
ferramentas de compilação. ZIP, RAR e 7z devem ser descompactados antes.
Para atualizar, extraia em outra pasta e selecione seu ambiente anterior em
Configurações > Selecionar ambiente. Confirme a atualização apresentada.
Seus jogos, dashboards, perfis e saves serão preservados; preparações válidas
serão reutilizadas. Uma atualização interrompida pode ser retomada abrindo
a mesma versão do aplicativo. Não misture arquivos de ZIPs diferentes.

A pasta app contém componentes internos. data guarda o ambiente padrão e suas
preferências. Não apague a pasta data anterior ao trocar o aplicativo.
Os componentes substituídos são preservados em .local/payload-update/backup
ou .local/payload-backups dentro do ambiente. Ferramentas/SDK também mantêm
cópias anteriores ao serem atualizados com sua autorização de preparação.

Nenhum conteúdo proprietário do Xbox 360 está incluído. A preparação usa
ferramentas verificadas e pede autorização para downloads. Componentes Microsoft
podem exigir instalação adicional. Nada é enviado automaticamente.

Projeto independente, sem afiliação oficial com a Microsoft. Forneça apenas
conteúdo cuja utilização seja autorizada. O código próprio usa BSD-3-Clause:
consulte LICENSE. Avisos de terceiros: THIRD_PARTY_NOTICES.md e pasta licenses,
ao lado deste arquivo. Essas licenças não abrangem os arquivos originais Xbox.

Prévia experimental. Consulte as licenças e os limites documentados do projeto.
Windows limpo ainda requer validação. Serviços da dashboard, perfis e saves
continuam com as limitações registradas no relatório do projeto.
Código correspondente: $head
"@, [Text.UTF8Encoding]::new($false))
$metadata = [ordered]@{
    schema = 2; version = $Version; platform = 'windows-x64'; commit = $head
    payload_sha256 = Hash (Join-Path $payload 'payload.sha256')
    sdk_commit = $lock.rexglue_sdk.commit; patches = @($lock.rexglue_sdk.patches | ForEach-Object { [ordered]@{ file = $_.file; sha256 = $_.sha256 } })
    dependencies = $dependencies; compiler = [ordered]@{ version = $compilerVersion; sha256 = Hash $clang }; resource_compiler_sha256 = Hash $rcCompiler
    configuration = 'Release'; desktop_only = $true; relative_source_paths = $true; compiler_flags = '-march=x86-64-v2 -mno-incremental-linker-compatible -ffile-prefix-map=<source>=/x360-source'; linker_flags = '-Wl,/Brepro'
    publication = $Publication; clean_windows = 'pending'
}
if ($provenance) { $metadata.provenance = $provenance }
[IO.File]::WriteAllText((Join-Path $destination 'package.json'), ($metadata | ConvertTo-Json -Depth 8), [Text.UTF8Encoding]::new($false))
& (Join-Path $source 'scripts/check-package-licenses.ps1') -PackageDirectory $destination -SdkDirectory $sdk
WriteIndex $destination 'package.sha256'
# Deterministic ZIP: fixed timestamps, entry order and relative paths only.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = "$destination.zip"
$stream = [IO.File]::Open($zip, 'CreateNew')
$archive = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in Get-ChildItem -LiteralPath $destination -Recurse -File | Sort-Object FullName) {
        $rel = $name + '/' + $file.FullName.Substring($destination.Length + 1).Replace('\','/')
        $entry = $archive.CreateEntry($rel, [IO.Compression.CompressionLevel]::Optimal)
        $entry.LastWriteTime = [DateTimeOffset]::new(2000,1,1,0,0,0,[TimeSpan]::Zero)
        $output = $entry.Open(); $input = [IO.File]::OpenRead($file.FullName)
        try { $input.CopyTo($output) } finally { $input.Dispose(); $output.Dispose() }
    }
} finally { $archive.Dispose(); $stream.Dispose() }
[IO.File]::WriteAllText("$zip.sha256", (Hash $zip) + '  ' + [IO.Path]::GetFileName($zip) + "`n")
Write-Output "Candidate: $zip"
Write-Output "Build evidence: $build"
