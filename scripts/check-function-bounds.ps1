# Compares the extent of generated functions with the bounds documented in a
# title's analysis file. For each "0xADDR = {...}" entry, the generated
# DEFINE_REX_FUNC(sub_ADDR) is found and its instructions counted (one "// <insn>"
# comment per guest instruction, as written by function_graph.cpp); its extent is
# start + 4*count. Expected bounds come from the nearest preceding "Bounds: A-B"
# comment (inclusive end) or "Bounds: N instructions each".
# Usage: scripts/check-function-bounds.ps1 -Analysis titles/<t>/analysis/<t>.toml -Generated titles/<t>/generated/<module>
param([Parameter(Mandatory)] [string]$Analysis, [Parameter(Mandatory)] [string]$Generated)
$ErrorActionPreference = 'Stop'
$entries = @(); $bounds = $null; $each = $null
foreach ($line in Get-Content $Analysis) {
    if ($line -match 'Bounds: (0x[0-9A-F]{8})-(0x[0-9A-F]{8})') { $bounds = @($Matches[1], $Matches[2]); $each = $null }
    elseif ($line -match 'Bounds: (\d+) instructions each') { $each = [int]$Matches[1]; $bounds = $null }
    if ($line -match '^0x([0-9A-F]{8}) = ') {
        $entries += [pscustomobject]@{ addr = $Matches[1]; bounds = $bounds; each = $each }
        if ($bounds) { $bounds = $null }   # a range applies to the entry right after it
    }
}
$bodies = @{}
foreach ($f in Get-ChildItem $Generated -Filter '*.cpp') {
    $cur = $null; $n = 0
    foreach ($l in [IO.File]::ReadLines($f.FullName)) {
        if ($l -match '^DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\)') { $cur = $Matches[1]; $n = 0; continue }
        if ($null -eq $cur) { continue }
        if ($l -eq '}') { $bodies[$cur] = $n; $cur = $null; continue }
        if ($l -match '^\t// ') { $n++ }
    }
}
$bad = 0
foreach ($e in $entries) {
    $start = [Convert]::ToUInt32($e.addr, 16)
    if (-not $bodies.ContainsKey($e.addr)) { '{0}  NOT GENERATED' -f $e.addr; $bad++; continue }
    $end = $start + 4 * $bodies[$e.addr] - 1
    $exp = if ($e.bounds) { [Convert]::ToUInt32($e.bounds[1].Replace('0x', ''), 16) }
           elseif ($e.each) { $start + 4 * $e.each - 1 } else { $null }
    $ok = if ($null -eq $exp) { 'no bounds documented' } elseif ($exp -eq $end) { 'ok' } else { $bad++; 'MISMATCH (expected end 0x{0:X8})' -f $exp }
    '0x{0}  {1,3} insns  0x{0}-0x{2:X8}  {3}' -f $e.addr, $bodies[$e.addr], $end, $ok
}
'{0} entries, {1} problem(s)' -f $entries.Count, $bad
if ($bad) { exit 1 }
