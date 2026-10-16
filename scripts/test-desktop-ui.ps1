# Focused functional test of the desktop manager's controls (local, no input injection).
# Uses an isolated throw-away environment (copy of titles/ configuration only), so the
# user's graphics.toml, library, profiles and saves are never touched.
# Usage: powershell -ExecutionPolicy Bypass -File scripts/test-desktop-ui.ps1 -Exe <x360-native.exe>
param([Parameter(Mandatory)] [string]$Exe, [string]$Repo = (Get-Location).Path)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System; using System.Text; using System.Runtime.InteropServices;
public static class X360T {
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("kernel32.dll")] public static extern IntPtr GlobalAlloc(uint f, UIntPtr s);
  [DllImport("kernel32.dll")] public static extern IntPtr GlobalLock(IntPtr m);
  [DllImport("kernel32.dll")] public static extern bool GlobalUnlock(IntPtr m);
}
'@
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("x360-ui-test-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $tmp | Out-Null
Copy-Item (Join-Path $Repo 'deps.lock.json') $tmp
Copy-Item (Join-Path $Repo 'titles') (Join-Path $tmp 'titles') -Recurse
$failures = 0
function Check($name, $ok, $detail = '') { if ($ok) { Write-Host "PASS $name" } else { Write-Host "FAIL $name $detail"; $script:failures++ } }
$p = Start-Process $Exe -ArgumentList @('--root', $tmp, '--lang', 'pt') -PassThru
try {
    $h = [IntPtr]::Zero
    for ($i = 0; $i -lt 60 -and $h -eq [IntPtr]::Zero; $i++) { $p.Refresh(); $h = $p.MainWindowHandle; Start-Sleep -Milliseconds 500 }
    if ($h -eq [IntPtr]::Zero) { throw 'no window' }
    Start-Sleep 6
    function Cmd([int]$id) { [void][X360T]::SendMessage($h, 0x111, [IntPtr]$id, [IntPtr]::Zero); Start-Sleep -Milliseconds 500 }
    function Text([int]$id) { $sb = New-Object System.Text.StringBuilder 512; [void][X360T]::GetWindowText([X360T]::GetDlgItem($h, $id), $sb, 512); $sb.ToString() }
    function Rows { [int][X360T]::SendMessage([X360T]::GetDlgItem($h, 122), 0x18B, [IntPtr]::Zero, [IntPtr]::Zero) }
    function Preview([int]$s) { [void][X360T]::SendMessage($h, 0x8014, [IntPtr]$s, [IntPtr]::Zero); Start-Sleep -Milliseconds 400 }
    $graphics = Join-Path $tmp '.local/graphics.toml'
    function Graphics { if (Test-Path $graphics) { (Get-Content $graphics -Raw) -replace '\s+', ' ' } else { '' } }
    $kHome = 118; $kGames = 117; $kDash = 116; $kImportPage = 119
    # Renderer: Native cannot be chosen without the development access; Xenos is the default.
    Cmd 132; Check 'native refused without development access' ((Graphics) -notmatch "renderer = 'native'")
    Cmd 124; Check 'development access enabled keeps Xenos' ((Graphics) -match "renderer = 'xenos'" -and (Graphics) -match 'experimental = true')
    Cmd 132; Check 'Native selectable after access' ((Graphics) -match "renderer = 'native'")
    Cmd 123; Check 'Xenos selectable again' ((Graphics) -match "renderer = 'xenos'")
    Cmd 132; Cmd 124; Check 'disabling access forces Xenos' ((Graphics) -match "renderer = 'xenos'" -and (Graphics) -match 'experimental = false')
    # Language
    Cmd 131; Check 'English labels' ((Text 118) -eq 'Home' -and (Text 117) -eq 'Library')
    Cmd 130; Check 'Portuguese labels' ((Text 118) -eq ("In" + [char]0xED + "cio") -and (Text 117) -eq 'Biblioteca')
    # Navigation
    Cmd $kGames; Check 'library page reachable' ([X360T]::IsWindowVisible([X360T]::GetDlgItem($h, 125)))
    Cmd $kDash; Check 'dashboard page hides search' (-not [X360T]::IsWindowVisible([X360T]::GetDlgItem($h, 125)))
    Cmd $kImportPage; Check 'import page shows file/folder choices' ([X360T]::IsWindowVisible([X360T]::GetDlgItem($h, 113)) -and [X360T]::IsWindowVisible([X360T]::GetDlgItem($h, 115)))
    # Library filters and search over synthetic in-memory rows (nothing is written).
    Cmd $kGames; Preview 6
    Check 'nine synthetic games listed' ((Rows) -eq 9) "rows=$(Rows)"
    Cmd 127; Check 'filter Ready = 3' ((Rows) -eq 3) "rows=$(Rows)"
    Cmd 128; Check 'filter To prepare = 2' ((Rows) -eq 2) "rows=$(Rows)"
    Cmd 129; Check 'filter Attention = 4' ((Rows) -eq 4) "rows=$(Rows)"
    Cmd 126; Check 'filter All = 9' ((Rows) -eq 9) "rows=$(Rows)"
    [void][X360T]::SendMessage([X360T]::GetDlgItem($h, 125), 0xC, [IntPtr]::Zero, 'forza'); Start-Sleep -Milliseconds 400
    Check 'search by name' ((Rows) -eq 1) "rows=$(Rows)"
    [void][X360T]::SendMessage([X360T]::GetDlgItem($h, 125), 0xC, [IntPtr]::Zero, '584111f7'); Start-Sleep -Milliseconds 400
    Check 'search by Title ID' ((Rows) -eq 2) "rows=$(Rows)"
    [void][X360T]::SendMessage([X360T]::GetDlgItem($h, 125), 0xC, [IntPtr]::Zero, 'zzzz'); Start-Sleep -Milliseconds 400
    Check 'search without match shows empty state' ((Rows) -eq 0)
    [void][X360T]::SendMessage([X360T]::GetDlgItem($h, 125), 0xC, [IntPtr]::Zero, ''); Start-Sleep -Milliseconds 400
    Check 'clearing search restores all' ((Rows) -eq 9)
    Preview 5
    # Busy / result presentation
    Cmd $kImportPage; Preview 1
    Check 'busy disables navigation' (-not [X360T]::IsWindowEnabled([X360T]::GetDlgItem($h, 117)))
    Check 'cancel available while importing' ([X360T]::IsWindowEnabled([X360T]::GetDlgItem($h, 109)))
    Preview 5; Preview 3
    Check 'error offers retry' ([X360T]::IsWindowVisible([X360T]::GetDlgItem($h, 133)))
    Cmd 134; Check 'dismiss hides the result' (-not [X360T]::IsWindowVisible([X360T]::GetDlgItem($h, 133)))
    Preview 5
    Check 'process still responsive' ($p.Responding)
} finally {
    if (-not $p.HasExited) { Stop-Process $p -Force }
}
# Real import path: a folder without a game exercises the worker, progress and result banner
# exactly as a drop would (the review-only --import option starts it once content is verified).
$notGame = Join-Path $tmp 'not-a-game'; New-Item -ItemType Directory $notGame | Out-Null
Set-Content (Join-Path $notGame 'readme.txt') 'not a game'
$p = Start-Process $Exe -ArgumentList @('--root', $tmp, '--import', $notGame) -PassThru
try {
    $h = [IntPtr]::Zero
    for ($i = 0; $i -lt 60 -and $h -eq [IntPtr]::Zero; $i++) { $p.Refresh(); $h = $p.MainWindowHandle; Start-Sleep -Milliseconds 500 }
    $deadline = (Get-Date).AddSeconds(120); $result = $false
    do { Start-Sleep -Milliseconds 400; $result = [X360T]::IsWindowVisible([X360T]::GetDlgItem($h, 134)) } until ($result -or (Get-Date) -gt $deadline)
    Check 'non-game import ends with a dismissible result' $result
    Check 'navigation re-enabled after the import' ([X360T]::IsWindowEnabled([X360T]::GetDlgItem($h, 117)))
    [void][X360T]::SendMessage($h, 0x111, [IntPtr]117, [IntPtr]::Zero); Start-Sleep -Milliseconds 600
    Check 'nothing was imported into the library' ([int][X360T]::SendMessage([X360T]::GetDlgItem($h, 122), 0x18B, [IntPtr]::Zero, [IntPtr]::Zero) -eq 0)
    Check 'process still responsive' ($p.Responding)
} finally {
    if (-not $p.HasExited) { Stop-Process $p -Force }
}
Remove-Item $tmp -Recurse -Force -ErrorAction SilentlyContinue
if ($failures) { Write-Host "$failures check(s) failed"; exit 1 }
Write-Host 'all desktop UI checks passed'
