# Copyright (c) 2026, x360-native. SPDX-License-Identifier: BSD-3-Clause
# Original pixel geometry; no fonts, external artwork, graphics libraries or timestamps.
param([string]$OutputPath = (Join-Path $PSScriptRoot '../../src/desktop/desktop.ico'),
      [switch]$Check)
$ErrorActionPreference = 'Stop'
# Hand-drawn 5x7 X and N, separated by one column, preserving the XN monogram.
$rows = @('10001010001','10001011001','01010011001','00100010101',
          '01010010011','10001010011','10001010001')
$sizes = @(16,32,48,64)
$images = @()
foreach($size in $sizes) {
    $s = [IO.MemoryStream]::new(); $w = [IO.BinaryWriter]::new($s)
    $maskStride = [int]([Math]::Ceiling($size / 32.0) * 4)
    $w.Write([int]40); $w.Write([int]$size); $w.Write([int]($size*2))
    $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([int]0)
    $w.Write([int]($size*$size*4)); 1..4 | ForEach-Object { $w.Write([int]0) }
    # Nearest-neighbour integer coordinates keep each resolution deterministic.
    for($y=$size-1; $y -ge 0; --$y) {
        for($x=0; $x -lt $size; ++$x) {
            $u=[int][Math]::Floor($x*16.0/$size)-2
            $v=[int][Math]::Floor($y*16.0/$size)-4
            $ink=$u -ge 0 -and $u -lt 11 -and $v -ge 0 -and $v -lt 7 -and $rows[$v][$u] -eq '1'
            if($ink) { $w.Write([byte[]]@(246,250,248,255)) }
            else { $w.Write([byte[]]@(128,120,0,255)) }
        }
    }
    $w.Write([byte[]]::new($maskStride*$size)); $w.Flush()
    $images += ,$s.ToArray(); $w.Dispose(); $s.Dispose()
}
$s=[IO.MemoryStream]::new(); $w=[IO.BinaryWriter]::new($s)
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset=6+16*$sizes.Count
for($i=0;$i -lt $sizes.Count;++$i) {
    $w.Write([byte]$sizes[$i]); $w.Write([byte]$sizes[$i]); $w.Write([uint16]0)
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
    Write-Output 'Original desktop icon verified (16/32/48/64 px).'
} else { [IO.File]::WriteAllBytes([IO.Path]::GetFullPath($OutputPath),$bytes) }
