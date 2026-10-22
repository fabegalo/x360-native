# Manual opt-in only. No synthetic input, no real saves and no game launch.
# Creates a disposable 17559 storage root (HDD, Memory Unit 7, USB 12) with one
# synthetic saved-game package, then opens the dashboard for a bounded session.
# Copy/move it through Settings -> System -> Storage; the root is preserved.
param(
    [ValidateRange(15,300)][int]$TimeoutSeconds = 180,
    [string]$TestStorageRoot
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$supervisor = Join-Path $root '.local/build/desktop-only/src/supervisor/x360n.exe'
$manifest = Join-Path $root 'titles/dash_17559/dash_17559_manifest.toml'
if (!(Test-Path -LiteralPath $supervisor -PathType Leaf)) { throw 'Build the Release desktop supervisor first.' }
$diagnostics = [IO.Path]::GetFullPath((Join-Path $root '.local/diagnostics'))
$markerName = '.storage-transfer-17559-test-storage'
$markerText = 'retail-17559-storage-transfer-test-v1'
if ($TestStorageRoot) {
    $storage = (Resolve-Path -LiteralPath $TestStorageRoot).Path
    if (!$storage.StartsWith($diagnostics + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        !(Test-Path -LiteralPath (Join-Path $storage $markerName) -PathType Leaf) -or
        [IO.File]::ReadAllText((Join-Path $storage $markerName)) -ne $markerText) {
        throw 'Reutilize somente uma pasta criada por este roteiro em .local/diagnostics.'
    }
} else {
    $name = 'storage-transfer-17559-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '-' + [Guid]::NewGuid().ToString('N')
    $storage = Join-Path $diagnostics $name
    $user = Join-Path $storage 'dash17559'
    foreach ($dir in 'storage/00000007', 'storage/0000000C') {
        [void][IO.Directory]::CreateDirectory((Join-Path $user $dir))
    }
    [IO.File]::WriteAllText((Join-Path $storage $markerName), $markerText)
    [IO.File]::WriteAllText((Join-Path $user 'storage.toml'), @'
version = 1
[hdd]
capacity_bytes = 8589934592
reserve_bytes = 16777216
[[device]]
id = 7
type = "mu"
name = "Test Memory Unit"
capacity_bytes = 536870912
reserve_bytes = 1048576
[[device]]
id = 12
type = "usb"
name = "Test USB"
capacity_bytes = 1073741824
'@)
    # Synthetic shared saved game (XUID 0, title 4D5309C9, type 1): a 1 MiB
    # file and the 0x148-byte XCONTENT_AGGREGATE_DATA header the SDK lists.
    $package = Join-Path $user '0000000000000000/4D5309C9/00000001/X360NCOPYTEST'
    [void][IO.Directory]::CreateDirectory($package)
    [IO.File]::WriteAllBytes((Join-Path $package 'payload.bin'), [byte[]]::new(1048576))
    $header = [byte[]]::new(0x148)
    function Put32([int]$offset, [uint32]$value) {
        $bytes = [BitConverter]::GetBytes($value); [Array]::Reverse($bytes)
        [Array]::Copy($bytes, 0, $header, $offset, 4)
    }
    Put32 0 1; Put32 4 1
    $display = [Text.Encoding]::BigEndianUnicode.GetBytes('Teste de copia x360n')
    [Array]::Copy($display, 0, $header, 8, $display.Length)
    $file = [Text.Encoding]::ASCII.GetBytes('X360NCOPYTEST')
    [Array]::Copy($file, 0, $header, 0x108, $file.Length)
    Put32 0x140 0x4D5309C9
    $headers = Join-Path $user '0000000000000000/4D5309C9/Headers/00000001'
    [void][IO.Directory]::CreateDirectory($headers)
    [IO.File]::WriteAllBytes((Join-Path $headers 'X360NCOPYTEST.header'), $header)
}
Write-Host "Armazenamento descartavel preservado ao encerrar: $storage"
Write-Host 'Teste manual: Configuracoes > Sistema > Armazenamento; copie e mova o item "Teste de copia x360n" entre HDD, MU e USB.'
Write-Host "Para conferir persistencia, execute novamente com -TestStorageRoot `"$storage`"."
Write-Host "O processo da dashboard tem limite de $TimeoutSeconds segundos."
& $supervisor session $manifest --skip-boot-intro --crash-stack --user-data-root $storage --timeout $TimeoutSeconds
$result = $LASTEXITCODE
$resultName = 'transfer-result-' + [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ') + '.txt'
[IO.File]::WriteAllText((Join-Path $storage $resultName), "supervisor_exit=$result`ntimeout_seconds=$TimeoutSeconds`n")
exit $result
