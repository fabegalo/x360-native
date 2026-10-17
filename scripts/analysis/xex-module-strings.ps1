# Lists names ending in .xex, .xzp or .dll found in a loaded XEX image (as written
# by `x360n-content dump-image`), in ASCII and UTF-16BE, with occurrence counts.
# A name here is a reference in the binary, not proof that the file is opened or
# required: that needs analysis of the code that uses it or a run.
# Usage: scripts/analysis/xex-module-strings.ps1 -Image image.bin -Out names.txt
param([Parameter(Mandatory)] [string]$Image, [Parameter(Mandatory)] [string]$Out)
$ErrorActionPreference = 'Stop'
$b = [IO.File]::ReadAllBytes($Image)
$pat = '[A-Za-z0-9_:\\.$ -]{1,120}\.(xex|xzp|dll)(?![A-Za-z0-9])'
$found = @{}
function Scan([string]$text, [string]$enc) {
    foreach ($m in [regex]::Matches($text, $pat, 'IgnoreCase')) {
        $k = "$enc`t$($m.Value)"; $script:found[$k] = 1 + [int]$script:found[$k]
    }
}
# ASCII: non-printable bytes become NUL so strings stay separate
$ascii = -join ($b | ForEach-Object { if ($_ -ge 0x20 -and $_ -lt 0x7F) { [char]$_ } else { "`0" } })
Scan $ascii 'ascii'
# UTF-16BE (the image is big-endian) at both alignments
foreach ($start in 0, 1) {
    $len = ($b.Length - $start) - (($b.Length - $start) % 2)
    $u = [Text.Encoding]::BigEndianUnicode.GetString($b, $start, $len)
    $u = [regex]::Replace($u, '[^ -~]', "`0")
    Scan $u 'utf16be'
}
$found.GetEnumerator() | Sort-Object Name | ForEach-Object { "{0}`t{1}" -f $_.Value, $_.Name } | Set-Content -Encoding utf8 $Out
