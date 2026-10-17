# Coarse statistics of window captures, used to tell "rendered" from "blank" and
# "animated" from "frozen". Samples a fixed grid; no external dependencies.
# Usage: scripts/diagnostics/image-stats.ps1 -A a.png [-B b.png]
#   prints distinct_colors_A (quantized to 32 levels/channel) and, with -B,
#   mean_abs_diff (0-255, averaged over channels and grid points).
param([Parameter(Mandatory)] [string]$A, [string]$B, [int]$GridX = 64, [int]$GridY = 36)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function Sample([string]$path) {
    $bmp = [System.Drawing.Bitmap]::FromFile((Resolve-Path $path).Path)
    try {
        $px = New-Object 'System.Collections.Generic.List[int[]]'
        for ($j = 0; $j -lt $GridY; $j++) {
            for ($i = 0; $i -lt $GridX; $i++) {
                $c = $bmp.GetPixel([int](($i + 0.5) * $bmp.Width / $GridX), [int](($j + 0.5) * $bmp.Height / $GridY))
                $px.Add(@([int]$c.R, [int]$c.G, [int]$c.B))
            }
        }
        return $px
    } finally { $bmp.Dispose() }
}

$pa = Sample $A
$distinct = ($pa | ForEach-Object { '{0},{1},{2}' -f ($_[0] -shr 3), ($_[1] -shr 3), ($_[2] -shr 3) } | Sort-Object -Unique).Count
"distinct_colors=$distinct"
if ($B) {
    $pb = Sample $B
    $sum = 0
    for ($k = 0; $k -lt $pa.Count; $k++) {
        for ($ch = 0; $ch -lt 3; $ch++) { $sum += [Math]::Abs($pa[$k][$ch] - $pb[$k][$ch]) }
    }
    # Invariant culture: the value is parsed by scripts/validation/regress-iruka.ps1.
    [string]::Format([Globalization.CultureInfo]::InvariantCulture, "mean_abs_diff={0:F3}", $sum / ($pa.Count * 3))
}
