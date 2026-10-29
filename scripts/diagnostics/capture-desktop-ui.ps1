# Captures the desktop manager's pages as PNGs for visual review (local only).
# Drives the window through ordinary WM_COMMAND messages and the review-only
# preview messages compiled into non-distribution builds; no input is injected
# and no user data is written. Usage:
#   powershell -ExecutionPolicy Bypass -File scripts/diagnostics/capture-desktop-ui.ps1 `
#     -Exe .local\build\c5-desktop\src\desktop\Release\x360-native.exe -OutDir docs\ui\after [-Dpi 96] [-Lang pt] [-Width 1220 -Height 800]
# -Sizes 1000x700,1920x1080 captures several window sizes from one launch, each into OutDir\<size>.
param(
    [Parameter(Mandatory)] [string]$Exe,
    [Parameter(Mandatory)] [string]$OutDir,
    [string]$Root = (Get-Location).Path,
    [int]$Dpi = 96,
    [string]$Lang = 'pt',
    [int]$Width = 1220,
    [int]$Height = 800,
    [string]$Sizes = "",
    [string]$Tag = "",
    [string]$Pages = 'home,library,library-many,library-empty,dashboard,import,import-busy,prepare-busy,import-error,import-done,settings,diagnostics',
    [int]$MaxWaitSeconds = 300
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class X360Ui {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
}
'@
if (-not [IO.Path]::IsPathRooted($OutDir)) { $OutDir = Join-Path (Get-Location).Path $OutDir }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$suffix = if ($Dpi -ne 96 -or $Lang -ne "pt") { "-$Dpi-$Lang" } else { "" }
if ($Tag) { $suffix += "-$Tag" }
$args = @('--root', $Root, '--dpi', "$Dpi", '--lang', $Lang)
$p = Start-Process $Exe -ArgumentList $args -PassThru
try {
    $h = [IntPtr]::Zero
    for ($i = 0; $i -lt 60 -and $h -eq [IntPtr]::Zero; $i++) { $p.Refresh(); $h = $p.MainWindowHandle; Start-Sleep -Milliseconds 500 }
    if ($h -eq [IntPtr]::Zero) { throw 'no window' }
    $scale = $Dpi / 96.0
    [void][X360Ui]::SetWindowPos($h, [IntPtr]::Zero, 40, 20, [int]($Width * $scale), [int]($Height * $scale), 0x0004)
    # Wait until the initial content check (worker thread) has finished: CPU goes idle.
    $deadline = (Get-Date).AddSeconds($MaxWaitSeconds); $quiet = 0; $last = $p.TotalProcessorTime
    Start-Sleep 5
    while ((Get-Date) -lt $deadline -and $quiet -lt 4) {
        Start-Sleep 2; $p.Refresh(); $now = $p.TotalProcessorTime
        if (($now - $last).TotalMilliseconds -lt 150) { $quiet++ } else { $quiet = 0 }
        $last = $now
    }
    # Listing is mostly I/O: Settings stays disabled until it ends.
    while ((Get-Date) -lt $deadline -and -not [X360Ui]::IsWindowEnabled([X360Ui]::GetDlgItem($h, 114))) { Start-Sleep -Milliseconds 500 }
    $dir = $OutDir
    function Shot($name) {
        Start-Sleep -Milliseconds 700
        $r = New-Object X360Ui+RECT; [void][X360Ui]::GetWindowRect($h, [ref]$r)
        $w = $r.R - $r.L; $ht = $r.B - $r.T
        $b = New-Object System.Drawing.Bitmap $w, $ht
        $g = [System.Drawing.Graphics]::FromImage($b); $dc = $g.GetHdc()
        [void][X360Ui]::PrintWindow($h, $dc, 2); $g.ReleaseHdc($dc); $g.Dispose()
        $b.Save((Join-Path $dir "$name$suffix.png")); $b.Dispose()
    }
    function Cmd([int]$id) { [void][X360Ui]::SendMessage($h, 0x111, [IntPtr]$id, [IntPtr]::Zero); Start-Sleep -Milliseconds 800 }
    # Ordinary wheel messages scroll the Settings cards (72 logical px per notch).
    function Wheel([int]$notches) { for ($n = 0; $n -lt $notches; $n++) { [void][X360Ui]::SendMessage($h, 0x20A, [IntPtr]([int](-120 * 65536)), [IntPtr]::Zero) }; Start-Sleep -Milliseconds 300 }
    function Preview([int]$state) { [void][X360Ui]::SendMessage($h, 0x8014, [IntPtr]$state, [IntPtr]::Zero); Start-Sleep -Milliseconds 500 }
    $kHome = 118; $kImportPage = 119; $kGames = 117; $kDashboards = 116; $kSettings = 114; $kDetails = 107
    $sizeList = if ($Sizes) { $Sizes.Split(',') } else { @("${Width}x$Height") }
    foreach ($size in $sizeList) {
    $sw, $sh = $size.Split('x')
    if ($Sizes) {
        $dir = Join-Path $OutDir $size; New-Item -ItemType Directory -Force $dir | Out-Null
        [void][X360Ui]::SetWindowPos($h, [IntPtr]::Zero, 40, 20, [int]([int]$sw * $scale), [int]([int]$sh * $scale), 0x0004)
        Start-Sleep -Milliseconds 500
    }
    foreach ($page in $Pages.Split(',')) {
        switch ($page) {
            'home'          { Cmd $kHome; Shot 'home' }
            'library'       { Cmd $kGames; Shot 'library' }
            'library-many'  { Cmd $kGames; Preview 6; Shot 'library-many' }
            'library-empty' { Preview 7; Shot 'library-empty'; Preview 5 }
            'dashboard'     { Cmd $kDashboards; Shot 'dashboard' }
            'import'        { Cmd $kImportPage; Shot 'import' }
            'import-busy'   { Cmd $kImportPage; Preview 1; Shot 'import-busy'; Preview 5 }
            'prepare-busy'  { Cmd $kGames; Preview 2; Shot 'prepare-busy'; Preview 5 }
            'import-error'  { Cmd $kImportPage; Preview 3; Shot 'import-error'; Preview 5 }
            'import-done'   { Cmd $kImportPage; Preview 4; Shot 'import-done'; Preview 5 }
            'settings'      { Cmd $kSettings; Shot 'settings'; Cmd $kSettings }
            'settings-mid'  { Cmd $kSettings; Wheel 6; Shot 'settings-mid'; Cmd $kSettings }
            'settings-end'  { Cmd $kSettings; Wheel 80; Shot 'settings-end'; Cmd $kSettings }
            'diagnostics'   { Cmd $kDetails; Shot 'diagnostics'; Cmd $kDetails }
        }
    }
    }
} finally {
    if (-not $p.HasExited) { Stop-Process $p -Force }
}
