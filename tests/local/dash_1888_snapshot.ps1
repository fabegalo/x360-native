# Focused original-content checks; never starts a dashboard or a game.
param()
$ErrorActionPreference = 'Stop'
$env:PSModulePath = Join-Path $PSHOME 'Modules'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$source = Join-Path $root '.local/inputs/dashboards/1888'
if (!(Test-Path -LiteralPath (Join-Path $source 'blades_retail/dash.xex'))) {
    Write-Output 'SKIP: local Retail 1888 originals not supplied'
    exit 77
}
$plan = Get-Content -LiteralPath "$root/titles/dash_1888/snapshot.json" -Raw | ConvertFrom-Json
$scratch = Join-Path $source ('.snapshot-test-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $scratch | Out-Null
$prepare = Join-Path $root 'scripts/prepare-dash-1888.ps1'
& $prepare -SourceRoot $source -Destination "$scratch/first"
& $prepare -SourceRoot $source -Destination "$scratch/second"
& $prepare -SourceRoot $source -Destination "$scratch/first"
foreach ($entry in $plan.files) {
    foreach ($directory in @('first','second')) {
        $hash = (Get-FileHash -LiteralPath "$scratch/$directory/$($entry.path)").Hash.ToLowerInvariant()
        if ($hash -ne $entry.sha256) { throw 'Non-deterministic 1888 snapshot' }
    }
}
function Reject([scriptblock]$Action, [string]$Reason) {
    try { & $Action } catch {
        if (!$_.Exception.Message.Contains($Reason)) { throw }
        return
    }
    throw "Expected rejection: $Reason"
}
# Preserve timestamp and size: cached metadata must not authenticate changed bytes.
$font = "$scratch/first/xenonjklatin.xtt"
$time = [IO.File]::GetLastWriteTimeUtc($font)
$bytes = [IO.File]::ReadAllBytes($font)
$bytes[100] = $bytes[100] -bxor 1
[IO.File]::WriteAllBytes($font, $bytes)
[IO.File]::SetLastWriteTimeUtc($font, $time)
Reject { & $prepare -Destination "$scratch/first" } 'incompatible original'
# The source layout still matches the 1888 archive, but the font is incompatible.
$badSource = "$scratch/incompatible"
New-Item -ItemType Directory "$badSource/blades_retail_full_readme","$badSource/blades_retail" | Out-Null
foreach ($entry in $plan.files) { [IO.File]::Copy((Join-Path $source $entry.source), (Join-Path $badSource $entry.source)) }
[IO.File]::WriteAllBytes("$badSource/blades_retail_full_readme/xenonjklatin.xtt", $bytes)
Reject { & $prepare -SourceRoot $badSource -Destination "$scratch/rejected" } 'incompatible original'
if (Test-Path -LiteralPath "$scratch/rejected") { throw 'Rejected source published a snapshot' }
Remove-Item -LiteralPath "$scratch/second/vk.xex"
Reject { & $prepare -Destination "$scratch/second" } 'missing original'
Reject { & $prepare -Destination "$root/.local/inputs/dashboards/6717/forbidden-1888" } 'must stay inside'
# Runtime/build guard must also refuse the same changed font, with no fallback.
$ErrorActionPreference = 'Continue' # Expected native stderr in Windows PowerShell 5.
$output = & cmake "-DROOT=$scratch/first" "-DHEADER=$root/titles/dash_1888/src/original_assets.h" '-DVERSION=2.0.1888.0' -P "$root/cmake/runtime/x360n_dashboard_assets.cmake" 2>&1 | Out-String
$ErrorActionPreference = 'Stop'
if ($LASTEXITCODE -eq 0 -or !($output -replace '\s+', ' ').Contains('incompatible xenonjklatin.xtt')) {
    throw "1888 build gate accepted a changed font: $output"
}
Write-Output "PASS: 22 exact originals; deterministic snapshots, idempotence, tampering, missing dependency, incompatible base, no partial publication and version isolation. Fixtures retained: $scratch"
