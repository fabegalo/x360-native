param([Parameter(Mandatory)][string]$PackageDirectory,
      [Parameter(Mandatory)][string]$SdkDirectory)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$scratch = Join-Path $root ('.local/evidence/constant-export-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force "$scratch/policy/scripts/packaging", "$scratch/policy/scripts/build", "$scratch/policy/licenses" | Out-Null
foreach ($file in @('check-package-licenses.ps1','excluded-constants.ps1','generate-desktop-icon.ps1')) {
    Copy-Item -LiteralPath (Join-Path $root "scripts/packaging/$file") -Destination "$scratch/policy/scripts/packaging"
}
Copy-Item -LiteralPath (Join-Path $root 'scripts/build/release-configuration.ps1') -Destination "$scratch/policy/scripts/build"
$policy = Get-Content (Join-Path $root 'licenses/excluded-loader-constants.json') -Raw | ConvertFrom-Json
# Only this isolated test policy adds a synthetic exclusion; production rules
# and its full gate remain intact. No real key is written into a fixture.
$value = [byte[]](101..116)
$sha = [Security.Cryptography.SHA256]::Create()
try { $hash = [BitConverter]::ToString($sha.ComputeHash($value)).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
$policy.rules += @{id='synthetic-test'; first_byte=$value[0]; sha256=$hash}
$policy | ConvertTo-Json -Depth 10 | Set-Content "$scratch/policy/licenses/excluded-loader-constants.json"
function Index($directory, $index) {
    $lines = @(Get-ChildItem -LiteralPath $directory -Recurse -File | Where-Object FullName -ne (Join-Path $directory $index) | Sort-Object FullName | ForEach-Object {
        (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant() + "`t" + $_.FullName.Substring($directory.Length + 1).Replace('\', '/')
    })
    [IO.File]::WriteAllText((Join-Path $directory $index), ($lines -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
}
$tokens = @($value | ForEach-Object { '0x' + $_.ToString('x2') })
foreach ($prefix in @('', '+', '-')) {
    $name = if ($prefix -eq '') { 'cpp' } elseif ($prefix -eq '+') { 'added-diff' } else { 'removed-diff' }
    $package = Join-Path $scratch $name
    Copy-Item -LiteralPath $PackageDirectory -Destination $package -Recurse
    $payload = Join-Path $package 'app/workspace'
    $fixture = $prefix + 'const auto synthetic[] = {' + ($tokens[0..7] -join ',') + ",`n" + $prefix + ($tokens[8..15] -join ',') + '};'
    [IO.File]::WriteAllText((Join-Path $payload 'synthetic.patch'), $fixture)
    Index $payload 'payload.sha256'
    $meta = Get-Content (Join-Path $package 'package.json') -Raw | ConvertFrom-Json
    $meta.payload_sha256 = (Get-FileHash (Join-Path $payload 'payload.sha256')).Hash.ToLowerInvariant()
    $meta | ConvertTo-Json -Depth 20 | Set-Content (Join-Path $package 'package.json')
    Index $package 'package.sha256'
    $failed = $false
    try { & "$scratch/policy/scripts/packaging/check-package-licenses.ps1" -PackageDirectory $package -SdkDirectory $SdkDirectory | Out-Null }
    catch { if ($_.Exception.Message -notlike 'Sensitive loader constant in:*synthetic.patch') { throw }; $failed = $true }
    if (-not $failed) { throw "Full gate accepted $name in exported workspace" }
    Write-Output "PASS full package gate rejected $name (valid rebuilt payload/package indexes)"
}
