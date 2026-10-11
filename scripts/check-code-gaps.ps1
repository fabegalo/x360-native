# Heuristic diagnostic: lists code not covered by any generated function.
# Frameless leaf functions reached only through pointers have no .pdata entry
# and may be missed by discovery; a later indirect call then fails with
# "Call to invalid or unregistered function at guest address ...".
#
# Extent of a generated function = start + 4 * (instruction comment lines);
# exact for contiguous functions, approximate for ones with chunks. A gap is
# reported when it contains a non-zero word. Results are candidates for review
# (check the disassembly), not facts.
# Usage: scripts/check-code-gaps.ps1 -Image img.bin -Generated <dir> -CodeStart 82050000 -CodeEnd 8214AE14
param(
    [Parameter(Mandatory)] [string]$Image,
    [Parameter(Mandatory)] [string]$Generated,
    [Parameter(Mandatory)] [string]$CodeStart,
    [Parameter(Mandatory)] [string]$CodeEnd,
    [string]$Base = '82000000'
)
$ErrorActionPreference = 'Stop'
$img = [IO.File]::ReadAllBytes((Resolve-Path $Image).Path)
$imgBase = [Convert]::ToUInt32($Base, 16)  # distinct name: PowerShell variables are case-insensitive
$cs = [Convert]::ToUInt32($CodeStart, 16); $ce = [Convert]::ToUInt32($CodeEnd, 16)
function Word([uint32]$a) { $o = [int]($a - $imgBase); ([uint32]$img[$o] -shl 24) -bor ([uint32]$img[$o+1] -shl 16) -bor ([uint32]$img[$o+2] -shl 8) -bor [uint32]$img[$o+3] }

$names = @{}
Get-ChildItem $Generated -Filter '*_init.cpp' | ForEach-Object {
    foreach ($m in [regex]::Matches((Get-Content -Raw $_.FullName), '\{\s*0x([0-9A-Fa-f]{8}),\s*(\w+)\s*\}')) { $names[$m.Groups[2].Value] = [Convert]::ToUInt32($m.Groups[1].Value, 16) }
}
$extent = @{}
foreach ($file in Get-ChildItem $Generated -Filter '*_recomp.*.cpp') {
    $start = $null; $count = 0
    foreach ($line in [IO.File]::ReadLines($file.FullName)) {
        if ($line.StartsWith('DEFINE_REX_FUNC(')) {
            if ($null -ne $start) { $extent[$start] = [Math]::Max([uint32]($start + 4 * $count), [uint32]($extent[$start] -as [uint32])) }
            $fn = $line.Substring(16, $line.IndexOf(')') - 16)
            $start = if ($fn -match '^sub_([0-9A-F]{8})$') { [Convert]::ToUInt32($Matches[1], 16) } elseif ($names.ContainsKey($fn)) { $names[$fn] } else { $null }
            $count = 0
            continue
        }
        if ($null -ne $start -and $line.StartsWith("`t// ")) { $count++ }
    }
    if ($null -ne $start) { $extent[$start] = [uint32]($start + 4 * $count) }
}
$starts = @($extent.Keys | Sort-Object)
Write-Host ("{0} generated functions with known addresses" -f $starts.Count)

$found = 0; $skippedData = 0
$imageEnd = [uint32]($imgBase + $img.Length)
$cursor = $cs
foreach ($s in ($starts + @($ce))) {
    if ($s -gt $cursor) {
        # first non-zero word in [cursor, s)
        for ($a = $cursor; $a -lt $s; $a += 4) {
            if ((Word $a) -ne 0) {
                # A first word that points into the image is data (e.g. EH tables), not code.
                if ((Word $a) -ge $imgBase -and (Word $a) -lt $imageEnd) { $skippedData++; break }
                $len = 0; for ($b = $a; $b -lt $s -and (Word $b) -ne 0; $b += 4) { $len++ }
                Write-Host ("GAP 0x{0:X8}-0x{1:X8}: {2} non-zero word(s) starting 0x{3:X8} (first word 0x{4:X8})" -f $cursor, $s, $len, $a, (Word $a))
                $found++
                break
            }
        }
    }
    if ($extent.ContainsKey($s) -and $extent[$s] -gt $cursor) { $cursor = $extent[$s] }
}
Write-Host ("{0} gap(s) with non-zero content in 0x{1:X8}-0x{2:X8}; {3} more skipped as pointer-like data" -f $found, $cs, $ce, $skippedData)
