# Focused, bounded native-window validation. No global input or Computer Use.
param(
  [Parameter(Mandatory)][string]$Executable,
  [Parameter(Mandatory)][string]$Root,
  [Parameter(Mandatory)][string]$OutputDirectory,
  [int]$ExpectedGames = 3,
  [string]$ImportFile,
  [string]$PrepareGame,
  [string]$SelectGame,
  [switch]$CaptureDetails,
  [int]$PreparationTimeoutSeconds = 1200
)
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$Root = (Resolve-Path -LiteralPath $Root).Path
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force $OutputDirectory | Out-Null
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class LibraryWindow {
 public delegate bool Callback(IntPtr h, IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr w, Callback cb, IntPtr p);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr w, StringBuilder b, int n);
 [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr w);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr w, int command);
 public static string Dump(IntPtr w) {
  var result=new StringBuilder();
  EnumChildWindows(w,(h,p)=> { var text=new StringBuilder(8192); GetWindowText(h,text,text.Capacity); result.AppendLine(GetDlgCtrlID(h)+": "+text); return true; },IntPtr.Zero);
  return result.ToString();
 }
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr w, int id);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr w, uint m, IntPtr a, IntPtr b);
 [DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SendMessageW")] public static extern IntPtr ReadText(IntPtr w, uint m, IntPtr a, StringBuilder b);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr w, uint m, IntPtr a, IntPtr b);
 [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr w);
 [DllImport("user32.dll")] public static extern bool EnumWindows(Callback cb, IntPtr p);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr w, out uint pid);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr w, StringBuilder b, int n);
 public static IntPtr Dialog(int pid) {
   IntPtr found=IntPtr.Zero;
   EnumWindows((h,p)=> { uint owner; GetWindowThreadProcessId(h,out owner); var c=new StringBuilder(64); GetClassName(h,c,64);
     if(owner==pid && c.ToString()=="#32770") found=h; return true; },IntPtr.Zero);
   return found;
 }
 [DllImport("kernel32.dll")] public static extern IntPtr GlobalAlloc(uint flags, UIntPtr size);
 [DllImport("kernel32.dll")] public static extern IntPtr GlobalLock(IntPtr memory);
 [DllImport("kernel32.dll")] public static extern bool GlobalUnlock(IntPtr memory);
}
'@
$results = @()
for ($pass=1; $pass -le 2; $pass++) {
  $process = Start-Process -FilePath $Executable -ArgumentList @('--root', ('"' + $Root + '"')) -PassThru -WindowStyle Normal
  try {
    $deadline = (Get-Date).AddSeconds(180)
    $window = [IntPtr]::Zero
    do {
      Start-Sleep -Milliseconds 250
      $process.Refresh()
      if ($process.HasExited) { throw 'Desktop exited before validation' }
      $window=$process.MainWindowHandle
      $library = if ($window -ne [IntPtr]::Zero) { [LibraryWindow]::GetDlgItem($window,121) } else { [IntPtr]::Zero }
      $rowsList = if ($window -ne [IntPtr]::Zero) { [LibraryWindow]::GetDlgItem($window,122) } else { [IntPtr]::Zero }
      # The list control always holds the dashboard rows once the first verification finished.
      $loaded = $rowsList -ne [IntPtr]::Zero -and [LibraryWindow]::SendMessage($rowsList,0x18B,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32() -gt 0
    } until (($loaded -and $library -ne [IntPtr]::Zero -and [LibraryWindow]::IsWindowEnabled($library)) -or (Get-Date) -gt $deadline)
    if ((Get-Date) -gt $deadline) { throw 'Desktop verification exceeded 180 seconds' }
    if ($pass -eq 1 -and $ImportFile) {
      $path=(Resolve-Path -LiteralPath $ImportFile).Path
      $bytes=[Text.Encoding]::Unicode.GetBytes($path + [char]0 + [char]0)
      $drop=[LibraryWindow]::GlobalAlloc(0x42,[UIntPtr]::new([uint64]($bytes.Length+20)))
      $ptr=[LibraryWindow]::GlobalLock($drop)
      [Runtime.InteropServices.Marshal]::WriteInt32($ptr,0,20)
      [Runtime.InteropServices.Marshal]::WriteInt32($ptr,16,1)
      [Runtime.InteropServices.Marshal]::Copy($bytes,0,[IntPtr]::Add($ptr,20),$bytes.Length)
      [void][LibraryWindow]::GlobalUnlock($drop)
      [void][LibraryWindow]::SendMessage($window,0x233,$drop,[IntPtr]::Zero)
      $deadline=(Get-Date).AddSeconds(180)
      do { Start-Sleep -Milliseconds 250 } until ([LibraryWindow]::IsWindowEnabled($library) -or (Get-Date) -gt $deadline)
      if ((Get-Date) -gt $deadline) { throw 'Import exceeded 180 seconds' }
    }
    [void][LibraryWindow]::SendMessage($window,0x111,[IntPtr]121,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 500
    $list=[LibraryWindow]::GetDlgItem($window,122)
    $count=[LibraryWindow]::SendMessage($list,0x18B,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32()
    if ($count -ne $ExpectedGames) {
      [LibraryWindow]::Dump($window) | Set-Content -LiteralPath (Join-Path $OutputDirectory "failed-pass$pass.txt")
      throw "Expected $ExpectedGames games, got $count on pass $pass"
    }
    $rows=@()
    for ($i=0;$i -lt $count;$i++) {
      $length=[LibraryWindow]::SendMessage($list,0x18A,[IntPtr]$i,[IntPtr]::Zero).ToInt32()
      $text=New-Object Text.StringBuilder ($length+1)
      [void][LibraryWindow]::ReadText($list,0x189,[IntPtr]$i,$text)
      $rows += $text.ToString()
    }
    if ($pass -eq 1 -and $PrepareGame) {
      $selected=-1
      for ($i=0;$i -lt $rows.Count;$i++) { if ($rows[$i].StartsWith($PrepareGame)) { $selected=$i; break } }
      if ($selected -lt 0) { throw "Imported game not found: $PrepareGame" }
      [void][LibraryWindow]::SendMessage($list,0x186,[IntPtr]$selected,[IntPtr]::Zero)
      [void][LibraryWindow]::SendMessage($window,0x111,[IntPtr](122 -bor (1 -shl 16)), $list)
      [void][LibraryWindow]::PostMessage($window,0x111,[IntPtr]113,[IntPtr]::Zero)
      $dialog=[IntPtr]::Zero; $deadline=(Get-Date).AddSeconds(10)
      do { Start-Sleep -Milliseconds 100; $dialog=[LibraryWindow]::Dialog($process.Id) } until ($dialog -ne [IntPtr]::Zero -or (Get-Date) -gt $deadline)
      if ($dialog -eq [IntPtr]::Zero) { throw 'Expected preparation consent dialog was not opened' }
      $prompt=[LibraryWindow]::Dump($dialog)
      $prompt | Set-Content -LiteralPath (Join-Path $OutputDirectory 'prepare-prompt.txt')
      if ($prompt -notmatch '905 MiB') { throw "Unexpected dialog: $prompt" }
      # No downloads or system installation: exercise verified local tools.
      [void][LibraryWindow]::PostMessage($dialog,0x111,[IntPtr]7,[IntPtr]::Zero)
      Start-Sleep -Seconds 2
      & (Join-Path $PSScriptRoot '../diagnostics/capture-window.ps1') -ProcessName $process.ProcessName -Out (Join-Path $OutputDirectory 'preparing.png') -WaitSeconds 5
      $deadline=(Get-Date).AddSeconds($PreparationTimeoutSeconds)
      while (-not [LibraryWindow]::IsWindowEnabled($library) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 1
        if ($process.HasExited) { throw 'Desktop exited during preparation' }
      }
      if ((Get-Date) -gt $deadline) {
        [void][LibraryWindow]::PostMessage($window,0x111,[IntPtr]109,[IntPtr]::Zero)
        $cancelDeadline=(Get-Date).AddSeconds(30)
        while (-not [LibraryWindow]::IsWindowEnabled($library) -and (Get-Date) -lt $cancelDeadline) { Start-Sleep -Seconds 1 }
        throw 'Preparation exceeded bounded test duration; cancellation requested'
      }
      [LibraryWindow]::Dump($window) | Set-Content -LiteralPath (Join-Path $OutputDirectory 'prepare-result.txt')
      [void][LibraryWindow]::SendMessage($window,0x111,[IntPtr]121,[IntPtr]::Zero)
      $rows=@()
      for ($i=0;$i -lt $count;$i++) {
        $text=New-Object Text.StringBuilder 2048
        [void][LibraryWindow]::ReadText($list,0x189,[IntPtr]$i,$text); $rows += $text.ToString()
      }
    }
    $rows | Set-Content -LiteralPath (Join-Path $OutputDirectory "library-pass$pass.txt") -Encoding UTF8
    if ($SelectGame) {
      $selected=-1
      for ($i=0;$i -lt $rows.Count;$i++) { if ($rows[$i].StartsWith($SelectGame)) { $selected=$i; break } }
      if ($selected -lt 0) { throw "Game not found: $SelectGame" }
      [void][LibraryWindow]::SendMessage($list,0x186,[IntPtr]$selected,[IntPtr]::Zero)
      [void][LibraryWindow]::SendMessage($window,0x111,[IntPtr](122 -bor (1 -shl 16)), $list)
    }
    [void][LibraryWindow]::ShowWindow($window,9)
    Start-Sleep -Milliseconds 300
    & (Join-Path $PSScriptRoot '../diagnostics/capture-window.ps1') -ProcessName $process.ProcessName -Out (Join-Path $OutputDirectory "library-pass$pass.png") -WaitSeconds 5
    if (-not (Test-Path -LiteralPath (Join-Path $OutputDirectory "library-pass$pass.png"))) { throw 'Capture failed' }
    if ($CaptureDetails) {
      [void][LibraryWindow]::SendMessage($window,0x111,[IntPtr]107,[IntPtr]::Zero)
      Start-Sleep -Milliseconds 300
      & (Join-Path $PSScriptRoot '../diagnostics/capture-window.ps1') -ProcessName $process.ProcessName -Out (Join-Path $OutputDirectory "details-pass$pass.png") -WaitSeconds 5
      [LibraryWindow]::Dump($window) | Set-Content -LiteralPath (Join-Path $OutputDirectory "details-pass$pass.txt")
    }
    $results += ,$rows
    [void][LibraryWindow]::PostMessage($window,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
    if (-not $process.WaitForExit(5000)) { throw 'Desktop did not close normally' }
  } finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    $process.Dispose()
  }
}
if (($results[0] -join "`n") -ne ($results[1] -join "`n")) { throw 'Library changed across desktop restart' }
Write-Output 'PASS: identical catalog rows across two independent desktop processes.'
