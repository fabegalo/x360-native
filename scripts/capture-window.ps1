# Saves a PNG of the main window of a running process (evidence for run reports).
# Local only; nothing is uploaded.
# Usage: powershell -ExecutionPolicy Bypass -File scripts/capture-window.ps1 -ProcessName iruka -Out shot.png [-WaitSeconds 20]
param(
    [Parameter(Mandatory)] [string]$ProcessName,
    [Parameter(Mandatory)] [string]$Out,
    [int]$WaitSeconds = 20
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class X360nWin {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
}
'@

$deadline = (Get-Date).AddSeconds($WaitSeconds)
$proc = $null
while ((Get-Date) -lt $deadline) {
    $proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue |
        Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1
    if ($proc) { break }
    Start-Sleep -Milliseconds 500
}
if (-not $proc) { Write-Error "no window for process '$ProcessName' within $WaitSeconds s"; exit 1 }

$rect = New-Object X360nWin+RECT
[void][X360nWin]::GetWindowRect($proc.MainWindowHandle, [ref]$rect)
$w = $rect.Right - $rect.Left; $h = $rect.Bottom - $rect.Top
if ($w -le 0 -or $h -le 0) { Write-Error "window has empty bounds"; exit 1 }
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
$ok = [X360nWin]::PrintWindow($proc.MainWindowHandle, $hdc, 2)  # PW_RENDERFULLCONTENT
$g.ReleaseHdc($hdc); $g.Dispose()
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
Write-Host "captured ${w}x${h} from pid $($proc.Id) (PrintWindow=$ok) -> $Out"
