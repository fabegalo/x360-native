// Optional preparation telemetry. A suspended child is assigned before it can
// spawn workers; closing the job terminates the entire tree, including on timeout.
using System;
using System.Diagnostics;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using System.Text.RegularExpressions;

public sealed class X360NPreparationMeasurement {
    public int exit_code;
    public double wall_seconds, cpu_seconds;
    public ulong read_bytes, write_bytes, read_operations, write_operations;
    public ulong peak_job_commit_bytes, peak_process_commit_bytes;
    public bool timed_out;
    public bool contaminated;
    public string[] competing_processes = new string[0];
}
public static class X360NPreparationProcess {
    [StructLayout(LayoutKind.Sequential)] struct MemoryStatus {
        public uint length,load; public ulong totalPhysical,availablePhysical,totalPage,availablePage,totalVirtual,availableVirtual,extended;
    }
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool GlobalMemoryStatusEx(ref MemoryStatus status);
    public static int ParallelJobs() {
        var memory=new MemoryStatus(); memory.length=(uint)Marshal.SizeOf(memory);
        Check(GlobalMemoryStatusEx(ref memory));
        // Leave 2 GiB to the UI/OS and budget 1.5 GiB per Clang worker. This is
        // a conservative launch budget, not a guarantee on arbitrary source TUs.
        ulong available=memory.availablePhysical>2147483648UL?memory.availablePhysical-2147483648UL:0;
        return Math.Max(1,Math.Min(Environment.ProcessorCount,(int)Math.Min(available/1610612736UL,Int32.MaxValue)));
    }
    [StructLayout(LayoutKind.Sequential)] struct Startup {
        public int cb; public IntPtr reserved, desktop, title;
        public uint x,y,xsize,ysize,xchars,ychars,fill,flags;
        public ushort show, reservedSize; public IntPtr reserved2, input, output, error;
    }
    [StructLayout(LayoutKind.Sequential)] struct ProcessInfo { public IntPtr process, thread; public uint pid, tid; }
    [StructLayout(LayoutKind.Sequential)] struct Limits {
        public long processTime, jobTime; public uint flags;
        public UIntPtr minWorkingSet, maxWorkingSet; public uint activeLimit;
        public UIntPtr affinity; public uint priority, scheduling;
    }
    [StructLayout(LayoutKind.Sequential)] struct Io { public ulong reads,writes,others,readBytes,writeBytes,otherBytes; }
    [StructLayout(LayoutKind.Sequential)] struct ExtendedLimits {
        public Limits basic; public Io io;
        public UIntPtr processLimit,jobLimit,peakProcess,peakJob;
    }
    [StructLayout(LayoutKind.Sequential)] struct Accounting {
        public long user,kernel,periodUser,periodKernel;
        public uint faults,total,active,terminated; public Io io;
    }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr CreateJobObject(IntPtr security, string name);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool SetInformationJobObject(IntPtr job,int kind,ref ExtendedLimits value,uint size);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool QueryInformationJobObject(IntPtr job,int kind,out Accounting value,uint size,IntPtr length);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool QueryInformationJobObject(IntPtr job,int kind,out ExtendedLimits value,uint size,IntPtr length);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool CreateProcess(string app,System.Text.StringBuilder command,IntPtr ps,IntPtr ts,bool inherit,uint flags,IntPtr env,string cwd,ref Startup startup,out ProcessInfo info);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool AssignProcessToJobObject(IntPtr job,IntPtr process);
    [DllImport("kernel32.dll", SetLastError=true)] static extern uint ResumeThread(IntPtr thread);
    [DllImport("kernel32.dll", SetLastError=true)] static extern uint WaitForSingleObject(IntPtr handle,uint milliseconds);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetExitCodeProcess(IntPtr process,out uint code);
    [DllImport("kernel32.dll")] static extern bool TerminateJobObject(IntPtr job,uint code);
    [DllImport("kernel32.dll")] static extern bool TerminateProcess(IntPtr process,uint code);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [StructLayout(LayoutKind.Sequential,CharSet=CharSet.Unicode)] struct ProcessEntry {
        public uint size,usage,pid; public UIntPtr heap; public uint module,threads,parent;
        public int priority; public uint flags;
        [MarshalAs(UnmanagedType.ByValTStr,SizeConst=260)] public string name;
    }
    [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr CreateToolhelp32Snapshot(uint flags,uint pid);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool Process32FirstW(IntPtr snapshot,ref ProcessEntry entry);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool Process32NextW(IntPtr snapshot,ref ProcessEntry entry);
    [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint access,bool inherit,uint pid);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool IsProcessInJob(IntPtr process,IntPtr job,out bool result);
    static string[] Competing(IntPtr job) {
        var result=new List<string>();
        IntPtr snapshot=CreateToolhelp32Snapshot(2,0); Check(snapshot!=new IntPtr(-1));
        try {
            var entry=new ProcessEntry(); entry.size=(uint)Marshal.SizeOf(entry);
            if(!Process32FirstW(snapshot,ref entry)) throw new Win32Exception(Marshal.GetLastWin32Error());
            do {
                if(!Regex.IsMatch(entry.name,@"^(clang.*|ninja|cmake|msbuild|cl|link|lld.*|rexglue|dash_.*|x360n(?:-extract|-content)?|g[0-9a-f]{16,})\.exe$",RegexOptions.IgnoreCase)) continue;
                bool own=false;
                if(job!=IntPtr.Zero) {
                    IntPtr handle=OpenProcess(0x0400,false,entry.pid);
                    if(handle!=IntPtr.Zero) {
                        try { Check(IsProcessInJob(handle,job,out own)); } finally { CloseHandle(handle); }
                    } else if(Marshal.GetLastWin32Error()==87) continue; // exited during snapshot
                }
                if(!own) result.Add(entry.name+" pid="+entry.pid);
            } while(Process32NextW(snapshot,ref entry));
        } finally { CloseHandle(snapshot); }
        return result.ToArray();
    }
    static void Check(bool success) { if(!success) throw new Win32Exception(Marshal.GetLastWin32Error()); }
    // A setup child can sample the job assigned by the GUI/measurement runner.
    // NULL queries its current job, including completed native descendants.
    public static X360NPreparationMeasurement CurrentJob() {
        Accounting accounting; ExtendedLimits peaks;
        Check(QueryInformationJobObject(IntPtr.Zero,8,out accounting,(uint)Marshal.SizeOf(typeof(Accounting)),IntPtr.Zero));
        Check(QueryInformationJobObject(IntPtr.Zero,9,out peaks,(uint)Marshal.SizeOf(typeof(ExtendedLimits)),IntPtr.Zero));
        return new X360NPreparationMeasurement {
            cpu_seconds=(accounting.user+accounting.kernel)/10000000.0,
            read_bytes=accounting.io.readBytes,write_bytes=accounting.io.writeBytes,
            read_operations=accounting.io.reads,write_operations=accounting.io.writes,
            peak_job_commit_bytes=peaks.peakJob.ToUInt64(),peak_process_commit_bytes=peaks.peakProcess.ToUInt64() };
    }
    public static X360NPreparationMeasurement Run(string application,string arguments,string cwd,int timeoutSeconds) {
        return Run(application,arguments,cwd,timeoutSeconds,false);
    }
    public static X360NPreparationMeasurement Run(string application,string arguments,string cwd,int timeoutSeconds,bool requireExclusive) {
        if(timeoutSeconds < 1) throw new ArgumentOutOfRangeException("timeoutSeconds");
        if(requireExclusive) {
            var competing=Competing(IntPtr.Zero);
            if(competing.Length!=0) throw new InvalidOperationException("Benchmark requires exclusive build/session access: "+String.Join(", ",competing));
        }
        IntPtr job=CreateJobObject(IntPtr.Zero,null); Check(job!=IntPtr.Zero);
        ProcessInfo process=new ProcessInfo(); var watch=Stopwatch.StartNew();
        try {
            var limits=new ExtendedLimits(); limits.basic.flags=0x2000;
            Check(SetInformationJobObject(job,9,ref limits,(uint)Marshal.SizeOf(limits)));
            var startup=new Startup(); startup.cb=Marshal.SizeOf(startup);
            Check(CreateProcess(application,new System.Text.StringBuilder("\""+application+"\" "+arguments),IntPtr.Zero,IntPtr.Zero,false,0x08000004,IntPtr.Zero,cwd,ref startup,out process));
            try { Check(AssignProcessToJobObject(job,process.process)); }
            catch { TerminateProcess(process.process,1); throw; }
            Check(ResumeThread(process.thread)!=0xffffffff);
            bool timeout=false; string[] contamination=new string[0]; double nextCheck=0;
            for(;;) {
                uint wait=WaitForSingleObject(process.process,250);
                if(wait==0) break;
                if(wait!=258) throw new Win32Exception(Marshal.GetLastWin32Error());
                if(requireExclusive && watch.Elapsed.TotalSeconds>=nextCheck) {
                    nextCheck=watch.Elapsed.TotalSeconds+2;
                    contamination=Competing(job);
                    if(contamination.Length!=0) { Check(TerminateJobObject(job,125)); Check(WaitForSingleObject(process.process,10000)==0); break; }
                }
                if(watch.Elapsed.TotalSeconds>=timeoutSeconds) { timeout=true; Check(TerminateJobObject(job,124)); Check(WaitForSingleObject(process.process,10000)==0); break; }
            }
            uint code; Check(GetExitCodeProcess(process.process,out code));
            Accounting accounting; Check(QueryInformationJobObject(job,8,out accounting,(uint)Marshal.SizeOf(typeof(Accounting)),IntPtr.Zero));
            ExtendedLimits peaks; Check(QueryInformationJobObject(job,9,out peaks,(uint)Marshal.SizeOf(typeof(ExtendedLimits)),IntPtr.Zero));
            return new X360NPreparationMeasurement { exit_code=timeout?124:contamination.Length!=0?125:unchecked((int)code),timed_out=timeout,
                contaminated=contamination.Length!=0,competing_processes=contamination,
                wall_seconds=watch.Elapsed.TotalSeconds,cpu_seconds=(accounting.user+accounting.kernel)/10000000.0,
                read_bytes=accounting.io.readBytes,write_bytes=accounting.io.writeBytes,
                read_operations=accounting.io.reads,write_operations=accounting.io.writes,
                peak_job_commit_bytes=peaks.peakJob.ToUInt64(),peak_process_commit_bytes=peaks.peakProcess.ToUInt64() };
        } finally { if(process.thread!=IntPtr.Zero) CloseHandle(process.thread); if(process.process!=IntPtr.Zero) CloseHandle(process.process); CloseHandle(job); }
    }
}
