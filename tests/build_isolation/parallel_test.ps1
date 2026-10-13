# Run through scripts/devshell.cmd. All compilers and artifacts are real; no
# proprietary inputs, no writes to an existing preparation or active build.
param([string]$Root = (Resolve-Path "$PSScriptRoot/../..").Path)
$ErrorActionPreference = 'Stop'
$env:PSModulePath = Join-Path $PSHOME 'Modules'
. "$Root/scripts/preparation-receipt.ps1"
$scratch = Join-Path $Root ('.local/build/isolation-' + [Guid]::NewGuid().ToString('N'))
$cmake = (Get-Command cmake).Source
$compiler = "$Root/.local/toolchains/llvm-22.1.8/bin/clang++.exe"
$eventName = 'Local\x360n-build-test-' + [Guid]::NewGuid().ToString('N')
$event = [Threading.EventWaitHandle]::new($false, 'ManualReset', $eventName)
function Assert($condition, $message) { if (-not $condition) { throw $message } }
function Configure($dir, $identity, $revision=0) {
    & $cmake -S $PSScriptRoot -B $dir -G Ninja "-DX360N_ROOT=$Root" "-DCMAKE_CXX_COMPILER=$compiler" "-DIDENTITY=$identity" "-DRUNTIME_REVISION=$revision" -DCMAKE_BUILD_TYPE=Debug "-DTEST_GATE=$eventName" *> "$dir-configure.log"
    Assert ($LASTEXITCODE -eq 0) "configure failed: $dir-configure.log"
}
function Build($dir, $label, $target='all') {
    $process = Start-Process -FilePath $cmake -ArgumentList "--build `"$dir`" --parallel 16 --target $target" -WindowStyle Hidden -PassThru -RedirectStandardOutput "$scratch/$label.log" -RedirectStandardError "$scratch/$label.err"
    $null = $process.Handle # Retain the exit code on Windows PowerShell 5.1.
    return $process
}
function Finish($process, $label) {
    Assert ($process.WaitForExit(120000)) "build timeout: $label"
    $process.Refresh()
    Assert ($process.ExitCode -eq 0) "build failed: $label ($($process.ExitCode)); $scratch"
}
try {
    [void][IO.Directory]::CreateDirectory($scratch)
    $a="$scratch/a"; $b="$scratch/b"
    Configure $a 17; Configure $b 23
    $pa=Build $a 'parallel-a'; $pb=Build $b 'parallel-b'
    Finish $pa 'parallel-a'; Finish $pb 'parallel-b'
    foreach ($dir in @($a,$b)) {
        Assert ((Get-ReceiptHash "$dir/rexruntime.dll") -eq (Get-ReceiptHash "$dir/sdk/Debug/rexruntime.dll")) 'staged DLL differs'
        foreach ($n in 1..16) { & "$dir/consumer$n.exe"; Assert ($LASTEXITCODE -eq 0) "wrong DLL: $dir consumer$n" }
    }
    $aFiles = @(Get-ReceiptEntries @("$a/rexruntime.dll", "$a/consumer1.exe"))
    $bFiles = @(Get-ReceiptEntries @("$b/rexruntime.dll", "$b/consumer1.exe"))
    Finish (Build $a 'noop-a') 'noop-a'; Finish (Build $b 'noop-b') 'noop-b'
    Assert ((Test-ReceiptEntries $aFiles) -and (Test-ReceiptEntries $bFiles)) 'no-op changed artifacts'
    Configure $b 23 1
    Finish (Build $b 'rebuild-b') 'rebuild-b'
    Assert (Test-ReceiptEntries $aFiles) 'another build overwrote independent artifacts'
    Assert (-not (Test-ReceiptEntries $bFiles)) 'changed component did not invalidate hashes'
    Assert ((Get-ReceiptHash "$b/consumer1.exe") -eq $bFiles[1].sha256) 'runtime-only rebuild changed the executable'
    Assert ((Get-ReceiptHash "$b/rexruntime.dll") -eq (Get-ReceiptHash "$b/sdk/Debug/rexruntime.dll")) 'runtime-only rebuild did not restage the DLL'
    & "$b/consumer1.exe"; Assert ($LASTEXITCODE -eq 0) 'consumer did not receive rebuilt DLL'
    # Hold a real Ninja invocation at a deterministic test barrier; a competing
    # cmake --build must fail before any build command can start.
    $first=Build $a 'owner' 'gate'
    $deadline=[DateTime]::UtcNow.AddSeconds(20)
    while (-not [IO.File]::Exists("$a/gate-started") -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 50 }
    Assert ([IO.File]::Exists("$a/gate-started")) 'owner never acquired build'
    $second=Build $a 'rejected'
    Assert ($second.WaitForExit(10000)) 'competing build did not reject promptly'
    $second.Refresh(); Assert ($second.ExitCode -ne 0) 'two producers accepted the same build'
    $rejection = [IO.File]::ReadAllText("$scratch/rejected.err") + [IO.File]::ReadAllText("$scratch/rejected.log")
    Assert ($rejection.Contains('Build unavailable:') -and $rejection.Contains((Join-Path $a '.x360n-build.lock'))) 'wrong reason for rejection'
    Assert (-not $first.HasExited) 'owner exited before releasing the barrier'
    [void]$event.Set(); Finish $first 'owner'
    Assert (Test-ReceiptEntries $aFiles) 'rejected producer changed artifacts'
    # A loaded/locked destination rejects publication and keeps its old bytes.
    $old = Get-ReceiptHash "$a/rexruntime.dll"
    $stage = "set(sources `"$($b.Replace('\','/'))/rexruntime.dll`")`nset(destination `"$($a.Replace('\','/'))`")`ninclude(`"$($Root.Replace('\','/'))/cmake/x360n_stage_files.cmake`")"
    [IO.File]::WriteAllText("$scratch/stage.cmake", $stage)
    $lock=[IO.File]::Open("$a/rexruntime.dll", 'Open', 'Read', 'Read')
    try {
        $ErrorActionPreference = 'Continue' # This native failure is the test.
        & $cmake -P "$scratch/stage.cmake" *> "$scratch/stage-locked.log"
        $failed=$LASTEXITCODE -ne 0
    } finally { $ErrorActionPreference = 'Stop'; $lock.Dispose() }
    Assert $failed 'locked destination unexpectedly replaced'
    Assert ((Get-ReceiptHash "$a/rexruntime.dll") -eq $old) 'failed stage corrupted destination'
    Assert (-not [IO.File]::Exists("$a/rexruntime.dll.stage-tmp")) 'failed stage leaked temporary file'
    Write-Output "PASS: 32 consumers, parallel builds, no-op, component rebuild, rejection of concurrent producer, atomic stage; evidence: $scratch"
} finally { [void]$event.Set(); $event.Dispose() }
