$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $root 'scripts/packaging/payload-content.ps1')
$scratch = Join-Path $root ('.local/evidence/payload-content-' + [guid]::NewGuid().ToString('N'))
$payload = Join-Path $scratch 'source'
New-Item -ItemType Directory -Force $payload | Out-Null
git -C $root archive --format=tar "--output=$scratch/source.tar" HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot export committed payload fixture' }
tar -xf "$scratch/source.tar" -C $payload
if ($LASTEXITCODE -ne 0) { throw 'Cannot extract committed payload fixture' }
# Exercise the exact pre-build gate against the real source export, including fonts.
Assert-SourcePayload $payload
$fonts = @($launcherFontHashes.PSObject.Properties)
if ($fonts.Count -ne 2) { throw 'Expected the two reviewed launcher fonts' }
foreach ($font in $fonts) {
    $file = Join-Path $payload $font.Name
    if (-not (Test-LauncherFont $font.Name $file)) { throw "Reviewed font rejected: $($font.Name)" }
    foreach ($wrongPath in @('font.otf', 'font.ttf', 'font.xtt', "external/$($font.Name)", $font.Name.ToUpperInvariant())) {
        if (Test-LauncherFont $wrongPath $file) { throw "Unreviewed font path accepted: $wrongPath" }
    }
    [IO.File]::AppendAllText($file, 'changed')
    $rejected = $false
    try { Assert-SourcePayload $payload } catch {
        if ($_.Exception.Message -notlike 'Non-distributable payload entry:*') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw 'Modified font accepted' }
    Copy-Item -LiteralPath (Join-Path $root $font.Name) -Destination $file
}
foreach ($name in @('unreviewed.otf','console.ttf','console.xtt','original.xex','foreign.exe','foreign.dll')) {
    $file = Join-Path $payload $name
    [IO.File]::WriteAllText($file, 'synthetic')
    $rejected = $false
    try { Assert-SourcePayload $payload } catch {
        if ($_.Exception.Message -notlike 'Non-distributable payload entry:*') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw "Forbidden payload accepted: $name" }
    Remove-Item -LiteralPath $file
}
'PASS: committed payload, both reviewed font hashes, altered fonts and forbidden files'
