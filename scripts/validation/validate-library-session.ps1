# Bounded, script-driven validation of the existing original-dashboard session.
# Commands in OutputDirectory/command.json: {id,action:'key'|'shot'|'close',
# process:'dash_17559'|Game,key:'A'|'Right'|...,hold_ms:120,name:'evidence'}.
# 'close' is test cleanup, NEVER evidence of an original title return.
param(
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string[]]$Game = @('forza_horizon'),
    [string]$Supervisor = '.local/build/win-amd64/src/supervisor/Debug/x360n.exe',
    [ValidateRange(30,600)][int]$Seconds = 300,
    [switch]$SkipBootIntro
)
$ErrorActionPreference='Stop'
$root= (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(-not $out.StartsWith((Join-Path $root '.local\evidence\validation\'),[StringComparison]::OrdinalIgnoreCase)) { throw 'Evidence must be under .local/evidence/validation' }
if(-not $Game.Count -or @($Game | Where-Object { $_ -notmatch '^[a-z][a-z0-9_]*$' }).Count) { throw 'Invalid game process name' }
$allowedProcesses = @('dash_17559') + $Game
New-Item -ItemType Directory -Force $out | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class CycleWindow {
 public delegate bool Callback(IntPtr h, IntPtr p);
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left,top,right,bottom; }
 [DllImport("user32.dll")] public static extern bool EnumWindows(Callback cb,IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr w,Callback cb,IntPtr p);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h,uint flags);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] public struct ProcessEntry {
  public uint size,usage,pid; public UIntPtr heap; public uint module,threads,parent;
  public int priority; public uint flags;
  [MarshalAs(UnmanagedType.ByValTStr,SizeConst=260)] public string name;
 }
 [DllImport("kernel32.dll")] static extern IntPtr CreateToolhelp32Snapshot(uint flags,uint pid);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern bool Process32FirstW(IntPtr h,ref ProcessEntry entry);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern bool Process32NextW(IntPtr h,ref ProcessEntry entry);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
 public static bool Owned(int supervisor,int child) {
  IntPtr snap=CreateToolhelp32Snapshot(2,0);
  if(snap==new IntPtr(-1))return false;
  try {
   ProcessEntry e=new ProcessEntry();e.size=(uint)Marshal.SizeOf(typeof(ProcessEntry));
   if(Process32FirstW(snap,ref e)) do {
    if(e.pid==(uint)child)return e.parent==(uint)supervisor;
   } while(Process32NextW(snap,ref e));
   return false;
  } finally {CloseHandle(snap);}
 }
 public static IntPtr Find(int pid) {
  IntPtr found=IntPtr.Zero;
  Callback cb=(h,p)=>{uint owner;GetWindowThreadProcessId(h,out owner);if(owner==pid&&IsWindowVisible(h))found=h;return true;};
  EnumWindows((h,p)=>{cb(h,p);EnumChildWindows(h,cb,p);return true;},IntPtr.Zero);return found;
 }
}
'@
$keys=@{ A=@(0x20,0x39);B=@(0x08,0x0E);X=@(0x4C,0x26);Y=@(0x50,0x19);
 Start=@(0x0D,0x1C);Back=@(0x5A,0x2C);Guide=@(0x47,0x22);
 Up=@(0x55,0x16);Down=@(0x4A,0x24);Right=@(0x48,0x23);Left=@(0x4E,0x31);
 LB=@(0x31,0x02);RB=@(0x33,0x04);RT=@(0x45,0x12);LT=@(0x51,0x10);
 SteerLeft=@(0x41,0x1E);SteerRight=@(0x44,0x20) }
$si=[Diagnostics.ProcessStartInfo]::new()
$si.FileName=if([IO.Path]::IsPathRooted($Supervisor)){$Supervisor}else{Join-Path $root $Supervisor}
$si.WorkingDirectory=$root;$si.UseShellExecute=$false;$si.CreateNoWindow=$true
$si.RedirectStandardOutput=$true;$si.RedirectStandardError=$true
$si.Arguments='session titles/dash_17559/dash_17559_manifest.toml --timeout '+$Seconds
if ($SkipBootIntro) { $si.Arguments += ' --skip-boot-intro' }
foreach($binding in @{UP='U';DOWN='J';RIGHT='H';LEFT='N'}.GetEnumerator()) {
 $si.EnvironmentVariables['REX_KEYBIND_DPAD_'+$binding.Key]=$binding.Value
}
$si.EnvironmentVariables['REX_KEYBIND_X']='L';$si.EnvironmentVariables['REX_KEYBIND_Y']='P'
$si.EnvironmentVariables['REX_KEYBIND_BACK']='Z';$si.EnvironmentVariables['REX_KEYBIND_GUIDE']='G'
$si.EnvironmentVariables['REX_LOG_LEVEL']='info'
$p=[Diagnostics.Process]::Start($si)
$stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync()
$deadline=(Get-Date).AddSeconds($Seconds);$last=''
@{pid=$p.Id;deadline=$deadline.ToString('o')} | ConvertTo-Json | Set-Content "$out/process.json"
try {
 while(!$p.HasExited -and (Get-Date) -lt $deadline) {
  $command=$null
  if(Test-Path "$out/command.json") {try {$command=Get-Content "$out/command.json" -Raw | ConvertFrom-Json}catch{}}
  if($command -and $command.id -ne $last) {
   $last=$command.id
   try {
    if($command.action -eq 'close') {
     'Window close requested as cleanup, not title return' | Add-Content "$out/cleanup.txt"
     [void]$p.CloseMainWindow();break
    }
    if($command.process -notin $allowedProcesses){throw 'Unowned title process'}
    $title=Get-Process -Name $command.process -ErrorAction Stop | Where-Object {[CycleWindow]::Owned($p.Id,$_.Id)} | Select-Object -First 1
    if(!$title){throw 'Title is not a child of this supervised session'}
    $child=[CycleWindow]::Find($title.Id)
    if($child -eq [IntPtr]::Zero){throw 'Title has no visible session surface'}
    $frame=[CycleWindow]::GetAncestor($child,2)
    if($command.action -eq 'key') {
     $k=$keys[$command.key];if(!$k){throw 'Unknown controller binding'}
     $hold=if($command.hold_ms){[Math]::Min(3000,[Math]::Max(30,[int]$command.hold_ms))}else{120}
     [void][CycleWindow]::SetForegroundWindow($frame)
     [void][CycleWindow]::PostMessage($child,7,[IntPtr]::Zero,[IntPtr]::Zero)
     Start-Sleep -Milliseconds 60
     [void][CycleWindow]::PostMessage($child,0x100,[IntPtr]$k[0],[IntPtr](1 -bor ($k[1] -shl 16)))
     Start-Sleep -Milliseconds $hold
     [void][CycleWindow]::PostMessage($child,0x101,[IntPtr]$k[0],[IntPtr]([int64]0xC0000001 -bor ($k[1] -shl 16)))
    } elseif($command.action -eq 'shot') {
     if($command.name -notmatch '^[a-z0-9-]+$'){throw 'Invalid capture name'}
     $r=[CycleWindow+Rect]::new();[void][CycleWindow]::GetWindowRect($frame,[ref]$r)
     $bmp=[Drawing.Bitmap]::new($r.right-$r.left,$r.bottom-$r.top)
     $g=[Drawing.Graphics]::FromImage($bmp);$dc=$g.GetHdc()
     try {$ok=[CycleWindow]::PrintWindow($frame,$dc,2)}finally{$g.ReleaseHdc($dc);$g.Dispose()}
     try {if(!$ok){throw 'PrintWindow failed'};$bmp.Save("$out/$($command.name).png")}finally{$bmp.Dispose()}
    } else {throw 'Unknown validation action'}
    $result=@{id=$last;time=(Get-Date).ToString('o');process=$title.ProcessName;pid=$title.Id;frame=$frame.ToInt64();child=$child.ToInt64();action=$command.action;key=$command.key;hold_ms=$(if($command.action -eq 'key'){$hold}else{0});name=$command.name;ok=$true}
   }catch{$result=@{id=$last;ok=$false;error=$_.Exception.Message}}
   $result | ConvertTo-Json -Compress | Add-Content "$out/actions.jsonl"
   $result | ConvertTo-Json | Set-Content "$out/result.json"
  }
  Start-Sleep -Milliseconds 200
 }
 if(!$p.HasExited){
  'Bounded cleanup; not original return' | Add-Content "$out/cleanup.txt"
  [void]$p.CloseMainWindow()
  if(!$p.WaitForExit(10000)){taskkill /PID $p.Id /T /F | Out-Null}
 }
}finally{
 if(!$p.HasExited){taskkill /PID $p.Id /T /F | Out-Null}
 $stdout.Result | Set-Content "$out/supervisor.log"
 $stderr.Result | Set-Content "$out/supervisor.err"
 "exit=$($p.ExitCode)" | Set-Content "$out/exit.txt"
}
