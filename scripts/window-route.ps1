# Scripted route for a window we launched: posts key messages to that window
# only (no global input) and saves captures at given times. Used for menu tests
# of titles run with --mnk_mode=true (SDK keyboard -> pad emulation).
# Steps: "<seconds>:key:<Name>" or "<seconds>:shot:<file.png>", comma-separated,
# times measured from when the window first appears.
# Usage: scripts/window-route.ps1 -ProcessName castle_crashers -Steps "10:key:Return,15:shot:a.png"
param(
    [Parameter(Mandatory)] [string]$ProcessName,
    [Parameter(Mandatory)] [string]$Steps,
    [int]$WaitSeconds = 60
)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class X360nKeys {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  public delegate bool Callback(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumWindows(Callback cb, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr w, Callback cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr w, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr w);
  public static IntPtr Find(int pid) {
    IntPtr result=IntPtr.Zero;
    Callback visit=(h,p)=> { uint owner; GetWindowThreadProcessId(h,out owner); if(owner==pid && IsWindowVisible(h)) result=h; return true; };
    EnumWindows((h,p)=> { visit(h,p); EnumChildWindows(h,visit,p); return true; },IntPtr.Zero);
    return result;
  }
}
'@
# Virtual-key code and set-1 scan code (SDL maps keys from the scan code in lParam).
$keys = @{
    'Return' = @(0x0D, 0x1C); 'Space' = @(0x20, 0x39); 'X' = @(0x58, 0x2D)
    'W' = @(0x57, 0x11); 'A' = @(0x41, 0x1E); 'S' = @(0x53, 0x1F); 'D' = @(0x44, 0x20)
    'Backspace' = @(0x08, 0x0E); 'Semicolon' = @(0xBA, 0x27)
    'D1' = @(0x31, 0x02); 'D3' = @(0x33, 0x04)   # default LB / RB in --mnk_mode
}
$deadline = (Get-Date).AddSeconds($WaitSeconds)
$proc = $null
while ((Get-Date) -lt $deadline) {
    $proc = Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($proc) { $hwnd=[X360nKeys]::Find($proc.Id); if ($hwnd -ne [IntPtr]::Zero) { break } }
    Start-Sleep -Milliseconds 250
}
if (-not $proc -or $hwnd -eq [IntPtr]::Zero) { Write-Error "no window for '$ProcessName'"; exit 1 }
$t0 = Get-Date
foreach ($step in $Steps.Split(',')) {
    $parts = $step.Trim().Split(':', 3)
    $at = [double]$parts[0]
    $wait = $at - ((Get-Date) - $t0).TotalSeconds
    if ($wait -gt 0) { Start-Sleep -Milliseconds ([int]($wait * 1000)) }
    if (-not (Get-Process -Id $proc.Id -ErrorAction SilentlyContinue)) { Write-Host "process exited before step '$step'"; exit 2 }
    if ($parts[1] -eq 'key') {
        $k = $keys[$parts[2]]
        if (-not $k) { throw "unknown key $($parts[2])" }
        $down = [IntPtr](1 -bor ($k[1] -shl 16))
        $up = [IntPtr]([int64](1 -bor ($k[1] -shl 16) -bor (1 -shl 30)) -bor 0x80000000)
        [void][X360nKeys]::PostMessage($hwnd, 0x0100, [IntPtr]$k[0], $down)   # WM_KEYDOWN
        Start-Sleep -Milliseconds 120
        [void][X360nKeys]::PostMessage($hwnd, 0x0101, [IntPtr]$k[0], $up)     # WM_KEYUP
        Write-Host ("{0,6:N1}s key {1}" -f ((Get-Date) - $t0).TotalSeconds, $parts[2])
    } elseif ($parts[1] -eq 'shot') {
        & (Join-Path $PSScriptRoot 'capture-window.ps1') -ProcessName $ProcessName -Out $parts[2] -WaitSeconds 2 | Out-Null
        Write-Host ("{0,6:N1}s shot {1}" -f ((Get-Date) - $t0).TotalSeconds, $parts[2])
    }
}
