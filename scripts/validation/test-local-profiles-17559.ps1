# Opt-in visual validation of original 17559 UI using local window messages.
# Requires an isolated data directory (may be empty). All mutations come from
# the original guest UI. Run only with explicit visual/input authorization.
# Keys: A=32 B=8 Start=13 Down=74 Up=85 Right=72 Left=75 RB=51 X=76 Guide=71.
# A wait:<milliseconds> sequence item lets original transitions settle (max 10s).
param(
    [Parameter(Mandatory)] [string]$UserDataRoot,
    [Parameter(Mandatory)] [string]$Out,
    [string]$Sequence = '32',
    [switch]$Hosted,
    [string[]]$ExpectedProfiles,
    [AllowEmptyString()] [string]$ExpectedActive,
    [string]$GameRoot,
    [string]$Executable
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (!$GameRoot) { $GameRoot = Join-Path $root '.local/inputs/dashboards/17559/runtime' }
if (!$Executable) { $Executable = Join-Path $root '.local/build/titles/dash_17559/dash_17559.exe' }
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$GameRoot = (Resolve-Path -LiteralPath $GameRoot).Path
$UserDataRoot = (Resolve-Path -LiteralPath $UserDataRoot).Path
if (Test-Path -LiteralPath $Out) { throw 'Use a new evidence directory.' }
New-Item -ItemType Directory $Out | Out-Null
$Out = (Resolve-Path -LiteralPath $Out).Path
$binaryRoot = Split-Path $Executable
Get-FileHash -Algorithm SHA256 -Path $Executable,
    "$binaryRoot/rexruntime*.dll", "$binaryRoot/dash_17559_signin.dll",
    "$binaryRoot/dash_17559_createprofile.dll", "$binaryRoot/dash_17559_vk.dll",
    "$binaryRoot/dash_17559_gamerprofile.dll", "$binaryRoot/dash_17559_xam_ui.dll", "$binaryRoot/dash_17559_hud.dll" |
    Format-Table -AutoSize | Out-String -Width 240 | Set-Content "$Out/binaries.txt"
if (Get-Process -Name dash_17559 -ErrorAction SilentlyContinue) { throw 'Close the existing dashboard first.' }
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
  ShowWindow(h,9); BringWindowToTop(h); SetForegroundWindow(h); SetFocus(h);
  AttachThreadInput(own,target,false); AttachThreadInput(own,fg,false);
  return GetForegroundWindow()==h;
 }
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int mode);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
}
"@

$arguments = @('--gpu_plugin=xenos', '--mnk_mode=true', '--dash17559_boot_intro=false',
    '--keybind_dpad_up=U', '--keybind_dpad_right=H', '--keybind_dpad_down=J', '--keybind_dpad_left=K', '--keybind_guide=G', '--log_level=debug',
    "--game_data_root=`"$GameRoot`"", "--user_data_root=`"$UserDataRoot`"", "--log_file=`"$Out/rex.log`"")
if ($Hosted) { $arguments += '--dash17559_signin_ui=true' }
$process = $null
$watch = [Diagnostics.Stopwatch]::StartNew()
function Pause-Test([int]$Milliseconds) {
    if ($watch.ElapsedMilliseconds + $Milliseconds -gt 60000) { throw '60-second test limit exceeded.' }
    if ($process.WaitForExit($Milliseconds)) { throw 'Dashboard exited unexpectedly.' }
}
function Capture([string]$Name) {
    & "$root/scripts/diagnostics/capture-window.ps1" -ProcessName dash_17559 -Out "$Out/$Name.png" -WaitSeconds 1
    $profiles = @(Read-Profiles)
    ConvertTo-Json -InputObject $profiles | Set-Content "$Out/$Name.json"
    foreach ($version in 1,2) {
        $record = Join-Path $UserDataRoot "profiles/local-v$version.dat"
        if (Test-Path -LiteralPath $record) { Copy-Item -LiteralPath $record "$Out/$Name-v$version.dat" }
    }
}
function Read-Profiles {
    $record = Join-Path $UserDataRoot 'profiles/local-v2.dat'
    $v2 = Test-Path -LiteralPath $record
    if (!$v2) { $record = Join-Path $UserDataRoot 'profiles/local-v1.dat' }
    if (!(Test-Path -LiteralPath $record)) { return }
    $bytes = [IO.File]::ReadAllBytes($record)
    $count = 1; $offset = 8
    if ($v2) {
        if ($bytes.Length -lt 20) { throw 'Truncated profile store.' }
        $count = [int]$bytes[8]*16777216 + [int]$bytes[9]*65536 + [int]$bytes[10]*256 + [int]$bytes[11]
        $offset = 20
    }
    if ($count -lt 1 -or $count -gt 64 -or $bytes.Length -ne $offset + $count*25) { throw 'Invalid profile store length.' }
    for ($n=0; $n -lt $count; $n++) {
        $p = $offset + $n*25
        [pscustomobject]@{
            xuid = [BitConverter]::ToString($bytes, $p, 8).Replace('-', '')
            name = [Text.Encoding]::ASCII.GetString($bytes, $p+9, 16).TrimEnd([char]0)
            signedIn = $bytes[$p+8] -eq 1
        }
    }
}
function Press([int]$Key) {
    $process.Refresh()
    $window = $process.MainWindowHandle
    if (!$window) { throw 'The test dashboard has no window.' }
    $focused = $false
    if ([SigninInput]::GetForegroundWindow() -ne $window) {
    [void][SigninInput]::ShowWindow($window, 6)
    Pause-Test 250
    [void][SigninInput]::ShowWindow($window, 9)
    Pause-Test 250
    }
    for ($attempt = 0; $attempt -lt 8; $attempt++) {
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
    $i=0
    foreach($key in $Sequence.Split(',')) {
        if ($key -match '^wait:(\d+)$') {
            $delay = [int]$Matches[1]
            if ($delay -lt 1 -or $delay -gt 10000) { throw 'Wait must be between 1 and 10000ms.' }
            Pause-Test $delay
        } else {
            Press ([int]$key)
            Pause-Test 2500
        }
        Capture "step-$i"
        $i++
    }
    $profiles = @(Read-Profiles)
    if (Select-String -LiteralPath "$Out/rex.log" -Pattern 'Unhandled guest access violation|Unresolved guest|No compiled function|\[FATAL\]|Indirect failure' -Quiet) {
        throw 'Guest execution failed; screenshots are diagnostic evidence only.'
    }
    if ($PSBoundParameters.ContainsKey('ExpectedProfiles') -and
        (Compare-Object @($ExpectedProfiles | Sort-Object) @($profiles.name | Sort-Object))) {
        throw 'Persisted profiles do not match ExpectedProfiles.'
    }
    if ($PSBoundParameters.ContainsKey('ExpectedActive')) {
        $active = @($profiles | Where-Object signedIn)
        if (($ExpectedActive -eq '' -and $active.Count -ne 0) -or
            ($ExpectedActive -ne '' -and ($active.Count -ne 1 -or $active[0].name -cne $ExpectedActive))) {
            throw 'Persisted session does not match ExpectedActive.'
        }
    }
    'PASS: requested persistence assertions; inspect PNGs for original UI identity.' | Set-Content "$Out/result.txt"
} finally {
    if ($process -and !$process.HasExited) {
        Stop-Process -Id $process.Id -Force
        [void]$process.WaitForExit(2000)
    }
    "duration_ms=$($watch.ElapsedMilliseconds)" | Set-Content "$Out/run.txt"
}
