# Classifies the imports of an XEX against the pinned ReXGlue SDK.
# Input: the CSV printed by `x360n-content imports <root> <xex>` (ordinals as the
# SDK loader decodes them, with the SDK export table entry for each ordinal).
# Status per import, from the SDK sources under src/kernel/<lib>:
#   implemented   REX_EXPORT(__imp__<name>, ...)
#   raw hook      REX_HOOK_RAW(__imp__<name>) (implemented directly on ctx/base)
#   stub          REX_EXPORT_STUB(__imp__<name>) (logs "<name> STUB"; does not set r3)
#   stub+return   REX_EXPORT_STUB_RETURN (logs and sets r3 to a fixed value)
#   no shim       function in export_table.inc with none of the macros above in a
#                 source the runtime compiles: undefined symbol when a title links
#   variable mapped / unmapped   variable export with / without SetVariableMapping
#                 (unmapped: the loader writes 0xD000BEEF|ordinal and logs a warning)
#   absent        ordinal not in the SDK export table (codegen: "Cannot resolve ordinal")
# Usage: scripts/xex-imports.ps1 -ImportsCsv imports.csv [-Csv classified.csv]
param(
    [Parameter(Mandatory)] [string]$ImportsCsv,
    [string]$Csv
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Split-Path -Parent $PSScriptRoot)).Path
$sdk = Join-Path $root 'external\rexglue-sdk\src\kernel'

function Names([string]$src, [string]$pattern) {
    $h = @{}; foreach ($m in [regex]::Matches($src, $pattern)) { $h[$m.Groups[1].Value] = $true }; $h
}
# Only sources the runtime actually compiles count: src/kernel/CMakeLists.txt
# lists them, and at f5337cdc it leaves out (commented) xboxkrnl_usbcam.cpp,
# cert_monitor.cpp and debug_monitor.cpp, whose exports are then undefined at
# link time even though the macros exist in the tree.
$built = @{}
foreach ($line in Get-Content (Join-Path $sdk 'CMakeLists.txt')) {
    if ($line -match '^\s*((?:xam|xboxkrnl)/[\w/]+\.cpp)\b') { $built[$Matches[1]] = $true }
}
$macros = @{}
foreach ($lib in 'xam', 'xboxkrnl') {
    $files = Get-ChildItem (Join-Path $sdk $lib) -Recurse -Include *.cpp | Where-Object {
        $built.ContainsKey($_.FullName.Substring($sdk.Length + 1).Replace('\', '/'))
    }
    $src = ($files | Get-Content -Raw) -join "`n"
    $vars = @{}
    foreach ($m in [regex]::Matches($src, 'SetVariableMapping\(\s*"[^"]+",\s*0x([0-9A-Fa-f]+)')) {
        $vars[[Convert]::ToInt32($m.Groups[1].Value, 16)] = $true
    }
    $macros[$lib] = @{
        impl = Names $src 'REX_EXPORT\(__imp__(\w+),'
        raw = Names $src 'REX_HOOK_RAW\(__imp__(\w+)\)'
        stub = Names $src 'REX_EXPORT_STUB\(__imp__(\w+)\)'
        stubret = Names $src 'REX_EXPORT_STUB_RETURN\(__imp__(\w+),'
        vars = $vars
    }
}

# An XEX can list the same library in several import records (dash.xex has two for
# xam.xex) and the loader keeps each record apart, so one ordinal can show up once
# per record: e.g. only its value slot in one and only its thunk in the other. An
# import is a function if any record gives it a thunk.
$merged = Import-Csv $ImportsCsv | Group-Object library, ordinal | ForEach-Object {
    $f = $_.Group[0]
    $thunks = @($_.Group | Where-Object { $_.thunk_address -ne '0x00000000' })
    [pscustomobject]@{ library = $f.library; ordinal = $f.ordinal; sdk_name = $f.sdk_name; sdk_type = $f.sdk_type
        kind = $(if ($thunks.Count) { 'function' } else { 'variable' }); records = $_.Count }
}
$report = foreach ($r in $merged) {
    $m = $macros[$r.library]
    $ord = [Convert]::ToInt32($r.ordinal.Replace('0x', ''), 16)
    $status = if (-not $m) { 'library not in SDK' }
              elseif ($r.sdk_type -eq 'absent') { 'absent' }
              elseif ($r.sdk_type -eq 'variable') { if ($m.vars.ContainsKey($ord)) { 'variable mapped' } else { 'variable unmapped' } }
              elseif ($m.impl.ContainsKey($r.sdk_name)) { 'implemented' }
              elseif ($m.raw.ContainsKey($r.sdk_name)) { 'raw hook' }
              elseif ($m.stubret.ContainsKey($r.sdk_name)) { 'stub+return' }
              elseif ($m.stub.ContainsKey($r.sdk_name)) { 'stub' }
              else { 'no shim' }
    if ($r.kind -ne $r.sdk_type -and $r.sdk_type -ne 'absent') { $status += " (kind mismatch: xex $($r.kind))" }
    [pscustomobject]@{ library = $r.library; ordinal = $r.ordinal; kind = $r.kind; name = $r.sdk_name; status = $status; records = $r.records }
}
$report | Group-Object library, status | Sort-Object Name | ForEach-Object { '{0,-40} {1,5}' -f $_.Name, $_.Count }
'{0} imports' -f @($report).Count
if ($Csv) { $report | Export-Csv -NoTypeInformation -Encoding UTF8 $Csv; "csv: $Csv" }
