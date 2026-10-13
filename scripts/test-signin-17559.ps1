# Opt-in visual validation of original 17559 UI using local window messages.
# Requires an existing isolated profile store. Never creates/signs in a profile
# by modifying its record. Run only with explicit visual/input authorization.
param(
    [Parameter(Mandatory)] [string]$UserDataRoot,
    [Parameter(Mandatory)] [string]$Out,
    [ValidateSet('Confirm', 'Cancel', 'Restart')] [string]$Mode = 'Confirm',
    [switch]$Hosted,
    [switch]$ConfirmPrivacy,
    [switch]$GamerCard,
    [string]$GameRoot,
    [string]$Executable
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (!$GameRoot) { $GameRoot = Join-Path $root '.local/inputs/dashboard/17559' }
if (!$Executable) { $Executable = Join-Path $root '.local/build/titles/dash_17559/dash_17559.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$GameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
$UserDataRoot = (Resolve-Path -LiteralPath $UserDataRoot).Path
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path -LiteralPath $Out).Path
$binaryRoot = Split-Path $Executable
Get-FileHash -Algorithm SHA256 -Path $Executable,
    "$binaryRoot/rexruntime*.dll", "$binaryRoot/dash_17559_signin.dll",
    "$binaryRoot/dash_17559_gamerprofile.dll", "$binaryRoot/dash_17559_xam_ui.dll" |
    Format-Table -AutoSize | Out-String -Width 240 | Set-Content "$Out/binaries.txt"
if (Get-Process -Name dash_17559 -ErrorAction SilentlyContinue) { throw 'Close the existing dashboard first.' }
$record = Join-Path $UserDataRoot 'profiles/local-v1.dat'
$before = [IO.File]::ReadAllBytes($record)
if ($before.Length -ne 33 -or [Text.Encoding]::ASCII.GetString($before, 0, 8) -ne 'X360LP01') {
    throw 'Expected an existing local-v1 profile record.'
}
if (($Mode -eq 'Restart') -ne ($before[16] -eq 1)) { throw 'Unexpected initial session state for this test mode.' }
if ($Mode -ne 'Confirm' -and $ConfirmPrivacy) { throw 'Privacy confirmation requires Confirm mode.' }

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class SigninInput {
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr p);
 [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
 [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a,uint b,bool attach);
 [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr h);
 public static bool Focus(IntPtr h) {
  uint own=GetCurrentThreadId(), fg=GetWindowThreadProcessId(GetForegroundWindow(),IntPtr.Zero), target=GetWindowThreadProcessId(h,IntPtr.Zero);
  AttachThreadInput(own,fg,true); AttachThreadInput(own,target,true);
  BringWindowToTop(h); SetForegroundWindow(h); SetFocus(h);
  AttachThreadInput(own,target,false); AttachThreadInput(own,fg,false);
  return GetForegroundWindow()==h;
 }
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
}
"@

$arguments = @('--gpu_plugin=xenos', '--mnk_mode=true', '--dash17559_boot_intro=false',
    '--keybind_dpad_up=U', '--keybind_dpad_right=H', '--log_level=debug',
    "--game_data_root=`"$GameRoot`"", "--user_data_root=`"$UserDataRoot`"", "--log_file=`"$Out/rex.log`"")
if ($Hosted) { $arguments += '--dash17559_signin_ui=true' }
$process = $null
$watch = [Diagnostics.Stopwatch]::StartNew()
function Pause-Test([int]$Milliseconds) {
    if ($watch.ElapsedMilliseconds + $Milliseconds -gt 60000) { throw '60-second test limit exceeded.' }
    if ($process.WaitForExit($Milliseconds)) { throw 'Dashboard exited unexpectedly.' }
}
function Capture([string]$Name) {
    & "$PSScriptRoot/capture-window.ps1" -ProcessName dash_17559 -Out "$Out/$Name.png" -WaitSeconds 1
    Copy-Item -LiteralPath $record -Destination "$Out/$Name.dat"
}
function Press([int]$Key) {
    $process.Refresh()
    $window = $process.MainWindowHandle
    if (!$window) { throw 'The test dashboard has no window.' }
    $focused = $false
    for ($attempt = 0; $attempt -lt 3; $attempt++) {
        if ([SigninInput]::Focus($window)) { $focused = $true; break }
        Pause-Test 200
    }
    if (!$focused) { throw 'Could not focus the test dashboard.' }
    # SDL checks the real foreground window when processing WM_SETFOCUS.
    [void][SigninInput]::PostMessage($window, 7, [IntPtr]::Zero, [IntPtr]::Zero)
    Pause-Test 250
    # Zero scan code: a virtual keyboard event accepted by SDL's raw-input path.
    if (![SigninInput]::PostMessage($window, 0x100, [IntPtr]$Key, [IntPtr]1)) { throw 'Key down failed.' }
    Pause-Test 80
    if (![SigninInput]::PostMessage($window, 0x101, [IntPtr]$Key, [IntPtr]1)) { throw 'Key up failed.' }
    "elapsed_ms=$($watch.ElapsedMilliseconds) key=$Key pid=$($process.Id)" | Add-Content "$Out/keys.txt"
}
try {
    $process = Start-Process $Executable -WorkingDirectory (Split-Path $Executable) -WindowStyle Hidden -ArgumentList $arguments -PassThru
    Pause-Test 12000
    Capture 'before'
    if ($Mode -eq 'Confirm') {
        Press 0x20 # A
        Pause-Test 6500
        Capture 'after-a'
        if ($ConfirmPrivacy) {
            Press 0x55 # Up to Yes in the original first-use dialog
            Pause-Test 600
            Capture 'privacy-selection'
            Press 0x20
            Pause-Test 6500
        }
    } elseif ($Mode -eq 'Cancel') {
        Press 8 # B
        Pause-Test 4000
    }
    Capture 'dashboard'
    $after = [IO.File]::ReadAllBytes($record)
    if ($Mode -eq 'Cancel') {
        if ([Convert]::ToBase64String($after) -ne [Convert]::ToBase64String($before)) { throw 'Cancel modified the profile.' }
    } else {
        if ($after[16] -ne 1) { throw 'The original UI did not sign in the profile.' }
        foreach ($i in (0..15 + 17..32)) { if ($before[$i] -ne $after[$i]) { throw 'Profile identity changed.' } }
    }
    if ($GamerCard -and $Mode -ne 'Cancel') {
        1..3 | ForEach-Object { Press 0x33; Pause-Test 600 } # right shoulder to Settings
        Press 0x48 # Profile tile
        Pause-Test 600
        Capture 'settings'
        Press 0x20
        Pause-Test 5000
        Capture 'gamercard'
        Press 8
        Pause-Test 3000
        Capture 'gamercard-closed'
    }
    # This asserts persisted identity only. Inspect the real PNGs to validate
    # Home/navigation, original dialog contents and the displayed GamerCard.
    "mode=$Mode hosted=$Hosted initial_session=$($before[16]) final_session=$($after[16])" | Set-Content "$Out/result.txt"
} finally {
    if ($process -and !$process.HasExited) {
        Stop-Process -Id $process.Id -Force
        [void]$process.WaitForExit(2000)
    }
    "duration_ms=$($watch.ElapsedMilliseconds)" | Set-Content "$Out/run.txt"
}
