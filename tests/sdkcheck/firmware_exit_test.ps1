param([Parameter(Mandatory)][string]$Executable)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../.local/evidence/firmware'))
$run = Join-Path $root ('firmware-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
# 1 and 5 are the observed routines; 2 stands for one that is not modeled separately.
foreach ($routine in @(1, 5, 2)) {
    $case = Join-Path $run $routine
    New-Item -ItemType Directory -Path $case | Out-Null
    $stdout = Join-Path $case 'stdout.txt'
    $stderr = Join-Path $case 'stderr.txt'
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Executable
    $start.Arguments = "$routine `"$case`""
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        $null = $process.Start()
        $outTask = $process.StandardOutput.ReadToEndAsync()
        $errTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(10000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "Firmware probe $routine exceeded 10 seconds; evidence: $case"
        }
        $text = $outTask.Result
        [IO.File]::WriteAllText($stdout, $text)
        [IO.File]::WriteAllText($stderr, $errTask.Result)
        if ($process.ExitCode -ne 0 -or $text -notmatch "firmware request completed: $routine; callbacks=1; handoff=6" -or -not (Test-Path -LiteralPath "$case/handoff.bin")) {
            throw "Firmware probe $routine failed (exit $($process.ExitCode)); evidence: $case"
        }
    } finally { $process.Dispose() }
    Write-Output $text.Trim()
}
