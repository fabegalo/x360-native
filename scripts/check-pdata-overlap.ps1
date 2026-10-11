# Diagnostic: compares generated functions against the image's .pdata table.
# A generated function whose labels reach past the end its own .pdata entry
# declares (or into another .pdata function) was given wrong boundaries by the
# analysis; its gotos may point at labels emitted in a different function.
# Inputs: the loaded image (x360n-content dump-image) and a codegen output dir.
# Usage: scripts/check-pdata-overlap.ps1 -Image img.bin -Generated titles/<t>/generated/default [-Base 0x82000000]
param(
    [Parameter(Mandatory)] [string]$Image,
    [Parameter(Mandatory)] [string]$Generated,
    [string]$Base = '82000000'
)
$ErrorActionPreference = 'Stop'
$img = [IO.File]::ReadAllBytes((Resolve-Path $Image).Path)

function BE32([int]$o) { ([uint32]$img[$o] -shl 24) -bor ([uint32]$img[$o+1] -shl 16) -bor ([uint32]$img[$o+2] -shl 8) -bor [uint32]$img[$o+3] }

# PE section table (headers are little-endian in the loaded image).
$lfanew = [BitConverter]::ToUInt32($img, 0x3C)
$nsec = [BitConverter]::ToUInt16($img, $lfanew + 6)
$optSize = [BitConverter]::ToUInt16($img, $lfanew + 20)
$secTable = $lfanew + 24 + $optSize
$pdata = $null
for ($i = 0; $i -lt $nsec; $i++) {
    $h = $secTable + 40 * $i
    $name = [Text.Encoding]::ASCII.GetString($img, $h, 8).TrimEnd([char]0)
    if ($name -eq '.pdata') { $pdata = @{ va = [BitConverter]::ToUInt32($img, $h + 12); size = [BitConverter]::ToUInt32($img, $h + 8) } }
}
if (-not $pdata) { Write-Host 'no .pdata section'; exit 1 }

# Entries: BeginAddress, then PrologLen:8 | FuncLen:22 | flags:2 (big-endian, as in the image).
$ranges = New-Object 'System.Collections.Generic.SortedDictionary[uint32,uint32]'
for ($o = [int]$pdata.va; $o + 8 -le $pdata.va + $pdata.size; $o += 8) {
    $start = BE32 $o; $packed = BE32 ($o + 4)
    if ($start -eq 0) { continue }
    $len = (($packed -shr 8) -band 0x3FFFFF) * 4
    if ($len -gt 0) { $ranges[$start] = $start + $len }
}
$starts = [uint32[]]@($ranges.Keys)
Write-Host (".pdata at RVA 0x{0:X}, {1} bytes, {2} functions" -f $pdata.va, $pdata.size, $starts.Count)

function OwnerOf([uint32]$addr) {
    $lo = 0; $hi = $starts.Count - 1; $found = $null
    while ($lo -le $hi) { $mid = [int](($lo + $hi) / 2); if ($starts[$mid] -le $addr) { $found = $starts[$mid]; $lo = $mid + 1 } else { $hi = $mid - 1 } }
    if ($null -ne $found -and $addr -lt $ranges[$found]) { return $found }
    return $null
}

# Names other than sub_XXXXXXXX come from the mapping table in *_init.cpp.
$names = @{}
Get-ChildItem $Generated -Filter '*_init.cpp' | ForEach-Object {
    foreach ($m in [regex]::Matches((Get-Content -Raw $_.FullName), '\{\s*0x([0-9A-Fa-f]{8}),\s*(\w+)\s*\}')) { $names[$m.Groups[2].Value] = [Convert]::ToUInt32($m.Groups[1].Value, 16) }
}

$bad = 0; $checked = 0
foreach ($file in Get-ChildItem $Generated -Filter '*_recomp.*.cpp') {
    $fn = $null; $faddr = $null
    foreach ($line in [IO.File]::ReadLines($file.FullName)) {
        if ($line.StartsWith('DEFINE_REX_FUNC(')) {
            $fn = $line.Substring(16, $line.IndexOf(')') - 16)
            $faddr = if ($fn -match '^sub_([0-9A-F]{8})$') { [Convert]::ToUInt32($Matches[1], 16) } elseif ($names.ContainsKey($fn)) { $names[$fn] } else { $null }
            if ($null -ne $faddr -and $ranges.ContainsKey($faddr)) { $checked++ }
            continue
        }
        if ($null -eq $faddr -or -not $ranges.ContainsKey($faddr)) { continue }
        if ($line -match '^loc_([0-9A-F]{8}):') {
            $l = [Convert]::ToUInt32($Matches[1], 16)
            if ($l -ge $ranges[$faddr] -or $l -lt $faddr) {
                $owner = OwnerOf $l
                Write-Host ("OVERRUN {0} (.pdata 0x{1:X8}-0x{2:X8}) has label loc_{3:X8} in {4}{5}" -f $fn, $faddr, $ranges[$faddr], $l, $file.Name, $(if ($null -ne $owner) { " -> inside .pdata function 0x{0:X8}" -f $owner } else { '' }))
                $bad++
                $faddr = $null  # one line per function is enough
            }
        }
    }
}
Write-Host ("checked {0} generated functions that have a .pdata entry; {1} overrun(s)" -f $checked, $bad)
if ($bad) { exit 1 } else { exit 0 }
