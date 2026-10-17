# PID-scoped local input/capture, used only by explicitly requested benchmarks.
param([int]$CaptureProcessId=0,[string]$CapturePath)
Add-Type -AssemblyName System.Drawing
if(-not ('BenchmarkWindow' -as [type])) {
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BenchmarkWindow {
 public delegate bool Callback(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)] public struct Rect {public int left,top,right,bottom;}
 [DllImport("user32.dll")] public static extern bool EnumWindows(Callback cb,IntPtr p);
 [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h,Callback cb,IntPtr p);
 [DllImport("user32.dll")] public static extern IntPtr GetAncestor(IntPtr h,uint flags);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 public static IntPtr Find(int pid) {
  IntPtr found=IntPtr.Zero;
  Callback visit=(h,p)=>{uint owner;GetWindowThreadProcessId(h,out owner);if(owner==pid&&IsWindowVisible(h))found=h;return true;};
  EnumWindows((h,p)=>{visit(h,p);EnumChildWindows(h,visit,p);return true;},IntPtr.Zero);
  return found;
 }
}
'@
}
function Send-BenchmarkKey([int]$ProcessId,[int]$VirtualKey,[int]$ScanCode,[bool]$Down) {
 $h=[BenchmarkWindow]::Find($ProcessId);if($h -eq [IntPtr]::Zero){throw 'Benchmark window missing'}
 if($Down){
  [void][BenchmarkWindow]::SetForegroundWindow([BenchmarkWindow]::GetAncestor($h,2))
  [void][BenchmarkWindow]::PostMessage($h,7,[IntPtr]::Zero,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 60
 }
 $bits=1L -bor ($ScanCode -shl 16);$message=0x100
 if(!$Down){$bits=$bits -bor 0xC0000000L;$message=0x101}
 if(![BenchmarkWindow]::PostMessage($h,$message,[IntPtr]$VirtualKey,[IntPtr]$bits)){throw 'Key delivery failed'}
}
function Save-BenchmarkWindow([int]$ProcessId,[string]$Path) {
 $h=[BenchmarkWindow]::Find($ProcessId);if($h -eq [IntPtr]::Zero){throw 'Benchmark window missing'}
 $h=[BenchmarkWindow]::GetAncestor($h,2)
 $r=[BenchmarkWindow+Rect]::new();[void][BenchmarkWindow]::GetWindowRect($h,[ref]$r)
 $bmp=[Drawing.Bitmap]::new($r.right-$r.left,$r.bottom-$r.top)
 try {
  $g=[Drawing.Graphics]::FromImage($bmp);$dc=$g.GetHdc()
  try {$ok=[BenchmarkWindow]::PrintWindow($h,$dc,2)}finally{$g.ReleaseHdc($dc);$g.Dispose()}
  if(!$ok){throw 'PrintWindow failed'}
  $bmp.Save($Path)
 } finally {$bmp.Dispose()}
}
if($CaptureProcessId -gt 0) {Save-BenchmarkWindow $CaptureProcessId $CapturePath}
