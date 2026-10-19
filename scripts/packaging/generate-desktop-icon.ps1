# Copyright (c) 2026, x360-native. SPDX-License-Identifier: BSD-3-Clause
# Original geometry; no fonts, external artwork, graphics libraries or timestamps.
param([string]$OutputPath = (Join-Path $PSScriptRoot '../../src/desktop/resources/desktop.ico'),
      [switch]$Check)
$ErrorActionPreference = 'Stop'
# Same mark as the launcher sidebar: a white X on the interface accent green in
# a square with rounded corners. Both shapes are analytic: coverage comes from a
# 4x4 grid of samples per pixel, in integer eighths of a pixel, so every size is
# drawn directly (no resampling) and the bytes do not depend on the machine.
$sizes = @(16,20,24,32,48,64,96,128,256)
$accent = @(16,124,16)    # BGR of the palette accent RGB(16,124,16)
$ink = @(255,255,255)     # palette on_accent
$images = @()
foreach($size in $sizes) {
    $G = [long](8 * $size)
    $r = [long](2 * $size)                       # corner radius: a quarter of the side
    $x0 = [long][Math]::Floor($G * 29 / 100); $x1 = $G - $x0
    $y0 = [long][Math]::Floor($G * 27 / 100); $y1 = $G - $y0
    $t = [long][Math]::Floor($G * 55 / 1000)     # half of the stroke width
    $dx = $x1 - $x0; $dy = $y1 - $y0; $len2 = $dx*$dx + $dy*$dy; $limit = $t*$t*$len2; $r2 = $r*$r
    # Pixels farther than this from a stroke edge (in eighths) are uniformly in or out;
    # only edge pixels are sampled. X strokes are cut flat at the top and bottom.
    $margin = [long]6; $near = ($t + $margin)*($t + $margin)*$len2; $deep = ($t - $margin)*($t - $margin)*$len2
    $s = [IO.MemoryStream]::new(); $w = [IO.BinaryWriter]::new($s)
    $maskStride = [int]([Math]::Ceiling($size / 32.0) * 4)
    $mask = [byte[]]::new($maskStride*$size)
    $w.Write([int]40); $w.Write([int]$size); $w.Write([int]($size*2))
    $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([int]0)
    $w.Write([int]($size*$size*4)); 1..4 | ForEach-Object { $w.Write([int]0) }
    $corner = [int]($size / 4)
    # Every row starts as plain accent; only corner and stroke pixels are computed.
    $stride = 4*$size
    $plain = [byte[]]::new($stride)
    for($x=0; $x -lt $size; ++$x) { $plain[4*$x] = $accent[0]; $plain[4*$x+1] = $accent[1]; $plain[4*$x+2] = $accent[2]; $plain[4*$x+3] = 255 }
    $pixels = [byte[]]::new($stride*$size)
    $gx0 = [Math]::Max(0, [int][Math]::Floor(($x0 - 2*$t - 8) / 8)); $gx1 = [Math]::Min($size - 1, [int][Math]::Ceiling(($x1 + 2*$t + 8) / 8))
    for($y=$size-1; $y -ge 0; --$y) {             # DIB rows are bottom-up
        $row = $size - 1 - $y
        [Buffer]::BlockCopy($plain, 0, $pixels, $row*$stride, $stride)
        $cy = [long](8*$y + 4)
        $cornerRow = $y -lt $corner -or $y -ge $size - $corner
        $columns = [Collections.Generic.List[int]]::new()
        if($cornerRow) { for($x=0; $x -lt $corner; ++$x) { $columns.Add($x); $columns.Add($size - 1 - $x) } }
        if($cy -ge $y0 - $margin -and $cy -le $y1 + $margin) {
            for($x=$gx0; $x -le $gx1; ++$x) { if(-not $cornerRow -or ($x -ge $corner -and $x -lt $size - $corner)) { $columns.Add($x) } }
        }
        foreach($x in $columns) {
            $cx = [long](8*$x + 4)
            $a = ($cx - $x0)*$dy - ($cy - $y0)*$dx; $b = ($cx - $x1)*$dy + ($cy - $y0)*$dx
            $inkSamples = 0
            if($cy -ge $y0 + $margin -and $cy -le $y1 - $margin -and ($a*$a -le $deep -or $b*$b -le $deep)) { $inkSamples = 16 }
            elseif($cy -ge $y0 - $margin -and $cy -le $y1 + $margin -and ($a*$a -le $near -or $b*$b -le $near)) {
                foreach($j in 0..3) {
                    $py = $cy - 3 + 2*$j
                    if($py -lt $y0 -or $py -gt $y1) { continue }
                    foreach($i in 0..3) {
                        $px = $cx - 3 + 2*$i
                        $a = ($px - $x0)*$dy - ($py - $y0)*$dx; $b = ($px - $x1)*$dy + ($py - $y0)*$dx
                        if($a*$a -le $limit -or $b*$b -le $limit) { ++$inkSamples }
                    }
                }
            }
            $squareSamples = 16
            if($cornerRow -and ($x -lt $corner -or $x -ge $size-$corner)) {
                $squareSamples = 0
                foreach($j in 0..3) {
                    $py = $cy - 3 + 2*$j
                    $ey = $py - [Math]::Min([Math]::Max($py, $r), $G - $r)
                    foreach($i in 0..3) {
                        $px = $cx - 3 + 2*$i
                        $ex = $px - [Math]::Min([Math]::Max($px, $r), $G - $r)
                        if($ex*$ex + $ey*$ey -le $r2) { ++$squareSamples }
                    }
                }
            }
            $alpha = [int][Math]::Floor($squareSamples * 255 / 16)
            if($alpha -eq 0) { $mask[$row*$maskStride + ($x -shr 3)] = $mask[$row*$maskStride + ($x -shr 3)] -bor (0x80 -shr ($x % 8)) }
            $at = $row*$stride + 4*$x
            for($c=0; $c -lt 3; ++$c) { $pixels[$at + $c] = $accent[$c] + [Math]::Floor(($ink[$c] - $accent[$c]) * $inkSamples / 16) }
            $pixels[$at + 3] = $alpha
        }
    }
    $w.Write($pixels); $w.Write($mask); $w.Flush()
    $images += ,$s.ToArray(); $w.Dispose(); $s.Dispose()
}
$s=[IO.MemoryStream]::new(); $w=[IO.BinaryWriter]::new($s)
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset=6+16*$sizes.Count
for($i=0;$i -lt $sizes.Count;++$i) {
    $w.Write([byte]($sizes[$i] % 256)); $w.Write([byte]($sizes[$i] % 256)); $w.Write([uint16]0)
    $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([int]$images[$i].Length); $w.Write([int]$offset); $offset += $images[$i].Length
}
foreach($bytes in $images) { $w.Write([byte[]]$bytes) }; $w.Flush()
$bytes=$s.ToArray(); $w.Dispose(); $s.Dispose()
if($Check) {
    $existing=[IO.File]::ReadAllBytes([IO.Path]::GetFullPath($OutputPath))
    if([Convert]::ToBase64String($existing) -cne [Convert]::ToBase64String($bytes)) {
        throw 'Desktop icon differs from original reproducible geometry'
    }
    Write-Output "Original desktop icon verified ($($sizes -join '/') px)."
} else { [IO.File]::WriteAllBytes([IO.Path]::GetFullPath($OutputPath),$bytes) }
