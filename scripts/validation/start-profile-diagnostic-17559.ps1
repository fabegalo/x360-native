# Manual opt-in only. No synthetic input, original/profile copying or game launch.
# The supervisor owns the child Job Object and its bounded lifetime.
param(
    [ValidateRange(15,90)][int]$TimeoutSeconds = 90,
    [string]$TestStorageRoot
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$supervisor = Join-Path $root '.local/build/desktop-only/src/supervisor/x360n.exe'
$manifest = Join-Path $root 'titles/dash_17559/dash_17559_manifest.toml'
if (!(Test-Path -LiteralPath $supervisor -PathType Leaf)) { throw 'Build the Release desktop supervisor first.' }
$graphics = Join-Path $root '.local/graphics.toml'
if (Test-Path -LiteralPath $graphics) {
    $graphicsText = Get-Content -LiteralPath $graphics -Raw
    if ($graphicsText -notmatch '(?m)^\s*renderer\s*=\s*[''"]xenos[''"]\s*(?:#.*)?$' -or
        ($graphicsText -match '(?m)^\s*api\s*=' -and
         $graphicsText -notmatch '(?m)^\s*api\s*=\s*[''"]d3d12[''"]\s*(?:#.*)?$')) {
        throw 'Selecione Xenos e D3D12 nas configuracoes do aplicativo antes deste teste.'
    }
}
$diagnostics = [IO.Path]::GetFullPath((Join-Path $root '.local/diagnostics'))
$markerName = '.profile-p0-17559-test-storage'
if ($TestStorageRoot) {
    $storage = (Resolve-Path -LiteralPath $TestStorageRoot).Path
    if (!$storage.StartsWith($diagnostics + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        !(Test-Path -LiteralPath (Join-Path $storage $markerName) -PathType Leaf) -or
        [IO.File]::ReadAllText((Join-Path $storage $markerName)) -ne 'retail-17559-isolated-profile-test-v1') {
        throw 'Reutilize somente uma pasta de teste isolada criada por este roteiro em .local/diagnostics.'
    }
} else {
    $name = 'profile-17559-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + [Guid]::NewGuid().ToString('N')
    $storage = Join-Path $diagnostics $name
    [void][IO.Directory]::CreateDirectory($storage)
    [IO.File]::WriteAllText((Join-Path $storage $markerName), 'retail-17559-isolated-profile-test-v1')
}
Write-Host "Armazenamento de teste preservado ao encerrar: $storage"
Write-Host 'Teste manual: crie, alterne, renomeie e consulte perfis/conquistas pela interface original; nao inicie jogos neste roteiro.'
Write-Host "Para conferir persistencia, execute novamente com -TestStorageRoot `"$storage`"."
Write-Host "O processo da dashboard tem limite de $TimeoutSeconds segundos. Pilha e contexto PPC ficam na pasta da execucao."
& $supervisor session $manifest --skip-boot-intro --crash-stack --user-data-root $storage --timeout $TimeoutSeconds
$result = $LASTEXITCODE
$resultName = 'diagnostic-result-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + [Guid]::NewGuid().ToString('N') + '.txt'
[IO.File]::WriteAllText((Join-Path $storage $resultName), "supervisor_exit=$result`ntimeout_seconds=$TimeoutSeconds`n")
exit $result
