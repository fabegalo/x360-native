# Diagnostic: code addresses held by data words (vtables, callback tables) or formed
# in code (lis + addi/ori) that no generated function starts at. The codegen finds
# vtables only through RTTI
# (src/codegen/vtable_scanner.cpp, Complete Object Locators) and its general
# function-pointer scan is disabled (src/codegen/analyze.cpp), so a function that
# has no .pdata entry and is reached only through such a pointer is not generated;
# calling it fails with "Call to invalid or unregistered function at guest address".
#
# A target is classified FUNC only when all of these hold:
#   P  a data word holds it, or code forms it (input: the output of
#      `x360n-content xref <root> <xex> <code lo> <code hi>`; "data" and "addr" lines)
#   A  4-byte aligned
#   N  not inside any .pdata function (the image's exception directory)
#   G  not inside any generated function (extent as in scripts/analysis/check-code-gaps.ps1)
#   B  the word before it is 0 or ends a function (blr, bctr, unconditional b)
#   E  within 64 words: non-zero words up to a blr / bctr / unconditional b, without
#      reaching a .pdata or generated function start first
# Everything else is printed as NO with the failed criteria. FUNC is evidence for
# an analysis entry `0x... = {}` (start only, no size), not a fact about its extent.
#
# Usage: scripts/analysis/check-pointer-targets.ps1 -Image img.bin -Generated <dir> -Xref xref.txt
#            -Base 92000000 [-Toml]   (-Toml: only the FUNC entries, as TOML lines)
param(
    [Parameter(Mandatory)] [string]$Image,
    [Parameter(Mandatory)] [string]$Generated,
    [Parameter(Mandatory)] [string]$Xref,
    [string]$Base = '82000000',
    [switch]$Toml
)
$ErrorActionPreference = 'Stop'
$img = [IO.File]::ReadAllBytes((Resolve-Path $Image).Path)
$imgBase = [Convert]::ToUInt32($Base, 16)  # distinct name: PowerShell variables are case-insensitive
function Word([uint32]$a) { $o = [int]($a - $imgBase); ([uint32]$img[$o] -shl 24) -bor ([uint32]$img[$o+1] -shl 16) -bor ([uint32]$img[$o+2] -shl 8) -bor [uint32]$img[$o+3] }
function LE32([int]$o) { [uint32]$img[$o] -bor ([uint32]$img[$o+1] -shl 8) -bor ([uint32]$img[$o+2] -shl 16) -bor ([uint32]$img[$o+3] -shl 24) }
function IsEnd([uint32]$w) { ($w -eq 0x4E800020) -or ($w -eq 0x4E800420) -or ((($w -shr 26) -eq 18) -and (($w -band 3) -eq 0)) }

# .pdata (PE headers are little-endian; entries big-endian)
$opt = [int](LE32 0x3C) + 24
$pdRva = LE32 ($opt + 96 + 3 * 8); $pdSize = LE32 ($opt + 96 + 3 * 8 + 4)
$pdS = New-Object 'uint32[]' ([int]($pdSize / 8)); $pdE = New-Object 'uint32[]' $pdS.Length
for ($i = 0; $i -lt $pdS.Length; $i++) {
    $e = [uint32]($imgBase + $pdRva + 8 * $i)
    $pdS[$i] = Word $e
    $pdE[$i] = [uint32]($pdS[$i] + (((Word ($e + 4)) -shr 8) -band 0x3FFFFF) * 4)
}
[Array]::Sort($pdS, $pdE)

# Generated functions: start (PPCFuncMappings) and extent (instruction comments)
$names = @{}; $mapped = New-Object 'System.Collections.Generic.HashSet[uint32]'
Get-ChildItem $Generated -Filter '*_init.cpp' | ForEach-Object {
    foreach ($m in [regex]::Matches((Get-Content -Raw $_.FullName), '\{\s*0x([0-9A-Fa-f]{8}),\s*(\w+)\s*\}')) {
        $a = [Convert]::ToUInt32($m.Groups[1].Value, 16); $names[$m.Groups[2].Value] = $a; [void]$mapped.Add($a)
    }
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
$gS = [uint32[]]@($extent.Keys | Sort-Object); $gE = [uint32[]]@($gS | ForEach-Object { $extent[$_] })
$starts = New-Object 'System.Collections.Generic.HashSet[uint32]'
foreach ($s in $pdS) { [void]$starts.Add($s) }; foreach ($s in $gS) { [void]$starts.Add($s) }
function Inside([uint32[]]$S, [uint32[]]$E, [uint32]$a) {
    $lo = 0; $hi = $S.Length - 1; $best = -1
    while ($lo -le $hi) { $mid = [int](($lo + $hi) / 2); if ($S[$mid] -le $a) { $best = $mid; $lo = $mid + 1 } else { $hi = $mid - 1 } }
    return ($best -ge 0 -and $a -lt $E[$best])
}

# Targets, counted per target: data words ("data <addr> [<section>] holds 0x<target>")
# and addresses formed in code by lis + addi/ori ("addr <site> <word> ea 0x<target>")
$refs = @{}
foreach ($m in [regex]::Matches((Get-Content -Raw $Xref), '(?m)(?:holds|^addr\s.*?\bea) 0x([0-9A-Fa-f]{8})')) {
    $t = [Convert]::ToUInt32($m.Groups[1].Value, 16)
    if (-not $mapped.Contains($t)) { $refs[$t] = 1 + [int]$refs[$t] }
}

$func = 0; $no = 0
foreach ($a in ($refs.Keys | Sort-Object)) {
    $a = [uint32]$a
    if ($a % 4 -ne 0) { $no++; if (-not $Toml) { "NO   0x{0:X8} refs={1} unaligned" -f $a, $refs[$a] }; continue }
    $n = -not (Inside $pdS $pdE $a)
    $g = -not (Inside $gS $gE $a)
    $prev = Word ($a - 4)
    $b = ($prev -eq 0) -or (IsEnd $prev)
    $e = $false; $len = 0; $why = 'no end in 64 words'
    for ($k = 0; $k -lt 64; $k++) {
        $x = [uint32]($a + 4 * $k)
        if ($k -gt 0 -and $starts.Contains($x)) { $why = 'runs into a function start'; break }
        $w = Word $x
        if ($w -eq 0) { $why = 'zero word'; break }
        $len++
        if (IsEnd $w) { $e = $true; $why = ''; break }
    }
    if ($n -and $g -and $b -and $e) {
        $func++
        if ($Toml) { "0x{0:X8} = {{}}  # refs={1}; after 0x{2:X8}; first end after {3} word(s)" -f $a, $refs[$a], $prev, $len }
        else { "FUNC 0x{0:X8} refs={1} prev=0x{2:X8} words={3}" -f $a, $refs[$a], $prev, $len }
    } else {
        $no++
        if (-not $Toml) { "NO   0x{0:X8} refs={1} N={2} G={3} B={4} E={5} prev=0x{6:X8} {7}" -f $a, $refs[$a], [int]$n, [int]$g, [int]$b, [int]$e, $prev, $why }
    }
}
$summary = "{0} FUNC, {1} NO, of {2} unmapped code addresses referenced by data or code" -f $func, $no, $refs.Count
if ($Toml) { "# $summary" } else { $summary }
