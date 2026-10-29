param([string]$Root = (Resolve-Path "$PSScriptRoot/../..").Path)
$ErrorActionPreference = 'Stop'
Add-Type -Path "$Root/scripts/build/preparation-process.cs"
$jobs=[X360NPreparationProcess]::ParallelJobs()
if($jobs -lt 1 -or $jobs -gt [Environment]::ProcessorCount){throw 'Invalid adaptive parallelism'}
$scratch=Join-Path $Root ('.local/build/preparation-metrics-test-'+[Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($scratch)
$source=Join-Path $scratch 'source.bin'
[IO.File]::WriteAllBytes($source,[byte[]]::new(1048576))
$result=[X360NPreparationProcess]::Run($env:ComSpec,('/d /c copy /b "'+$source+'" "'+$scratch+'/copy.bin" >NUL'),$scratch,10)
if ($result.exit_code -ne 0 -or $result.wall_seconds -le 0 -or $result.cpu_seconds -lt 0 -or
    $result.peak_job_commit_bytes -eq 0 -or $result.read_bytes -lt 1048576 -or $result.write_bytes -lt 1048576 -or $result.read_operations -eq 0) { throw 'Incomplete process tree accounting' }
$result=[X360NPreparationProcess]::Run($env:ComSpec,'/d /c powershell.exe -NoProfile -Command "Start-Sleep -Seconds 20"',$scratch,1)
if (-not $result.timed_out -or $result.exit_code -ne 124 -or $result.wall_seconds -gt 12) { throw 'Preparation watchdog did not terminate its child tree' }
$result=[X360NPreparationProcess]::Run($env:ComSpec,'/d /c exit 17',$scratch,5)
if ($result.exit_code -ne 17 -or $result.timed_out) { throw 'Child failure masked as success' }
$cmake=Join-Path $Root '.local/toolchains/cmake-3.31.6/bin/cmake.exe'
$competitor=[Diagnostics.Process]::Start($cmake,'-E sleep 3')
try {
    $refused=$false
    try {[void][X360NPreparationProcess]::Run($env:ComSpec,'/d /c exit 0',$scratch,5,$true)}
    catch {if($_.Exception.ToString() -match 'exclusive build/session access'){$refused=$true}else{throw}}
    if(-not $refused){throw 'Concurrent benchmark was accepted'}
} finally {if(-not $competitor.WaitForExit(5000)){$competitor.Kill()};$competitor.Dispose()}
Write-Output 'PASS: tree CPU/I/O/read operations/peak commit, timeout and failure propagation'
