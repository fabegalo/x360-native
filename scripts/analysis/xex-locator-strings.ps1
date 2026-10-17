# Lists resource locator strings ("<scheme>://...") found in a loaded XEX image
# (as written by `x360n-content dump-image`), ASCII and UTF-16BE, with the address
# of each occurrence. section:// names a resource inside a loaded module
# (XexGetModuleSection, compare `x360n-content resources`); file:// names a file.
# A string is a candidate, not proof of use: the code that formats or passes it
# decides what is opened. Usage: scripts/analysis/xex-locator-strings.ps1 -Image img.bin -ImageBase 0x92000000
param([Parameter(Mandatory)] [string]$Image, [Parameter(Mandatory)] [string]$ImageBase)
$ErrorActionPreference = 'Stop'
$b = [IO.File]::ReadAllBytes($Image)
$base = [int64][Convert]::ToUInt32($ImageBase.Replace('0x', ''), 16)
$pat = '[A-Za-z]{2,12}://[\x21-\x7E]{0,160}'
$rows = @()
# ASCII: one char per byte, non-printable bytes become NUL so strings stay separate.
$chars = New-Object char[] $b.Length
for ($i = 0; $i -lt $b.Length; $i++) { $chars[$i] = if ($b[$i] -ge 0x20 -and $b[$i] -lt 0x7F) { [char]$b[$i] } else { [char]0 } }
foreach ($m in [regex]::Matches((New-Object string (,$chars)), $pat)) {
    $rows += [pscustomobject]@{ address = '0x{0:X8}' -f ($base + $m.Index); encoding = 'ascii'; text = $m.Value }
}
# UTF-16BE at both alignments.
foreach ($start in 0, 1) {
    $len = ($b.Length - $start) - (($b.Length - $start) % 2)
    $u = [regex]::Replace([Text.Encoding]::BigEndianUnicode.GetString($b, $start, $len), '[^ -~]', "`0")
    foreach ($m in [regex]::Matches($u, $pat)) {
        $rows += [pscustomobject]@{ address = '0x{0:X8}' -f ($base + $start + 2 * $m.Index); encoding = 'utf16be'; text = $m.Value }
    }
}
$rows | Sort-Object address
