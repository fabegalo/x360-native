# Copy only identified 1888 originals. Never patch originals or search other versions.
param(
    [string]$SourceRoot,
    [string]$Destination
)
$ErrorActionPreference = 'Stop'
$env:PSModulePath = Join-Path $PSHOME 'Modules'
$root = Split-Path -Parent $PSScriptRoot
if (!$SourceRoot) { $SourceRoot = Join-Path $root '.local/inputs/dashboards/1888' }
if (!$Destination) { $Destination = Join-Path $root '.local/inputs/dashboards/1888/runtime' }
$SourceRoot = [IO.Path]::GetFullPath($SourceRoot)
$Destination = [IO.Path]::GetFullPath($Destination)
$allowed = [IO.Path]::GetFullPath((Join-Path $root '.local/inputs/dashboards/1888'))
if (!$Destination.StartsWith($allowed + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw '1888 snapshot must stay inside .local/inputs/dashboards/1888'
}
$plan = Get-Content -LiteralPath (Join-Path $root 'titles/dash_1888/snapshot.json') -Raw | ConvertFrom-Json
if ($plan.schema -ne 1 -or $plan.dashboard_version -ne '2.0.1888.0' -or @($plan.files).Count -ne 22) {
    throw 'Invalid 1888 snapshot inventory'
}
function NoLinks([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force
    while ($item) {
        if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Link forbidden in original snapshot: $($item.FullName)" }
        $item = $item.Parent
        if (!$item -and (Test-Path -LiteralPath $Path -PathType Leaf)) {
            $item = (Get-Item -LiteralPath $Path).Directory
            $Path = $item.FullName
        }
    }
}
function Verify([string]$Path, $Entry) {
    if (![IO.File]::Exists($Path)) { throw "Retail 2.0.1888.0 missing original: $Path" }
    NoLinks $Path
    if ((Get-Item -LiteralPath $Path).Length -ne $Entry.size -or
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Entry.sha256) {
        throw "Retail 2.0.1888.0 incompatible original: $Path; expected SHA-256 $($Entry.sha256)"
    }
}
$names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($entry in $plan.files) {
    if ($entry.path -notmatch '^[a-z0-9_]+\.(xex|xtt|xzp)$' -or !$names.Add($entry.path) -or
        $entry.source -notmatch '^blades_retail(_full_readme)?/[a-z0-9_]+\.(xex|xtt|xzp)$' -or
        $entry.sha256 -notmatch '^[0-9a-f]{64}$' -or $entry.size -le 0) { throw 'Unsafe 1888 inventory entry' }
    # Validate every input before creating output; verification after copying
    # catches changes between source verification and the copy.
    Verify (Join-Path $SourceRoot $entry.source) $entry
}
if (Test-Path -LiteralPath $Destination) {
    NoLinks $Destination
    foreach ($entry in $plan.files) { Verify (Join-Path $Destination $entry.path) $entry }
    if (@(Get-ChildItem -LiteralPath $Destination -Force).Count -ne $names.Count) { throw 'Unexpected files in 1888 snapshot' }
    Write-Output "Verified existing Retail 1888 snapshot: $Destination"
    return
}
$parent = Split-Path -Parent $Destination
NoLinks $parent
$staging = Join-Path $parent ('.1888-snapshot-' + [Guid]::NewGuid().ToString('N'))
if (![IO.Path]::GetFullPath($staging).StartsWith($allowed + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw '1888 staging path escaped its version'
}
[void][IO.Directory]::CreateDirectory($staging)
foreach ($entry in $plan.files) {
    $out = Join-Path $staging $entry.path
    [IO.File]::Copy((Join-Path $SourceRoot $entry.source), $out, $false)
    Verify $out $entry
}
# Both resolved absolute paths were checked inside the 1888 directory above.
# A concurrent existing destination is an error; never overwrite it.
[IO.Directory]::Move($staging, $Destination)
Write-Output "Created verified Retail 1888 snapshot: $Destination"
