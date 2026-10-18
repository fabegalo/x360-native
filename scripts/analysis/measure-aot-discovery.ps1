# Analysis only: no guest execution, hints added, production codegen overwritten or receipts emitted.
param(
    [Parameter(Mandatory)] [string]$Probe,
    [Parameter(Mandatory)] [string]$Output,
    [int]$TimeoutSeconds = 600,
    [switch]$GenericOnly,
    [ValidateSet('charlie_murder', 'iruka', 'motocross_madness', 'castle_crashers')]
    [string[]]$Titles = @('charlie_murder', 'iruka', 'motocross_madness', 'castle_crashers')
)
$ErrorActionPreference = 'Stop'
if ($TimeoutSeconds -lt 1 -or $TimeoutSeconds -gt 600) { throw 'TimeoutSeconds must be 1..600' }
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$probePath = (Resolve-Path $Probe).Path
$identity = [ordered]@{
    schema_version=1; probe_sha256=(Get-FileHash $probePath -Algorithm SHA256).Hash.ToLower()
    runtime_sha256=(Get-FileHash (Join-Path (Split-Path $probePath) 'rexruntimed.dll') -Algorithm SHA256).Hash.ToLower()
    collector_sha256=(Get-FileHash $PSCommandPath -Algorithm SHA256).Hash.ToLower()
    probe_source_sha256=(Get-FileHash (Join-Path $root 'tests/tools/aot_discovery_probe.cpp') -Algorithm SHA256).Hash.ToLower()
    lock_sha256=(Get-FileHash (Join-Path $root 'deps.lock.json') -Algorithm SHA256).Hash.ToLower()
    sdk_commit=(& git -C (Join-Path $root 'external/rexglue-sdk') rev-parse HEAD)
}
$outputPath = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $outputPath) { throw 'Output already exists; preserve the previous measurement' }
[void][IO.Directory]::CreateDirectory($outputPath)
$records = @()
foreach ($title in $Titles) {
    $inputPath = Join-Path $root ".local/inputs/games/$title/content/default.xex"
    if (-not (Test-Path -LiteralPath $inputPath)) { throw "Missing corpus input: $inputPath" }
    $variants = @('generic')
    if (-not $GenericOnly -and $title -eq 'charlie_murder') { $variants += 'hostloader' }
    if (-not $GenericOnly -and $title -in @('motocross_madness', 'castle_crashers')) { $variants += 'switches' }
    foreach ($variant in $variants) {
        $modulePath = if ($variant -eq 'hostloader') { Join-Path (Split-Path $inputPath) 'HostLoader.dll' } else { $inputPath }
        $stem = "$title-$variant"
        $config = Join-Path $outputPath "$stem.toml"
        $text = "project_name='discovery'`nfile_path='$($modulePath.Replace('\','/'))'`nout_directory_path='unused'`n"
        # Preserve only the existing authoritative switch table; no function hints.
        if ($variant -eq 'switches') {
            $hints = Get-Content -Raw (Join-Path $root "titles/$title/analysis/$title.toml")
            $text += $hints.Substring($hints.IndexOf('[[switch_tables]]'))
        }
        [IO.File]::WriteAllText($config, $text)
        $graph = Join-Path $outputPath "$stem.tsv"
        $start = [Diagnostics.ProcessStartInfo]::new()
        $start.FileName = $probePath
        $start.Arguments = '"{0}" "{1}" 82163C60 82198CD8 829387E0 82ACC350 82114E20' -f $config, $graph
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        $process = [Diagnostics.Process]::new()
        $process.StartInfo = $start
        $timer = [Diagnostics.Stopwatch]::StartNew()
        try {
            [void]$process.Start()
            $stdout = $process.StandardOutput.ReadToEndAsync()
            $stderr = $process.StandardError.ReadToEndAsync()
            while (-not $process.WaitForExit(1000)) {
                if ($timer.Elapsed.TotalSeconds -gt $TimeoutSeconds) { throw "Probe timeout: $stem" }
            }
            $timer.Stop()
            [IO.File]::WriteAllText((Join-Path $outputPath "$stem.stdout.log"), $stdout.GetAwaiter().GetResult())
            [IO.File]::WriteAllText((Join-Path $outputPath "$stem.stderr.log"), $stderr.GetAwaiter().GetResult())
            if (-not (Test-Path -LiteralPath $graph)) { throw "Probe produced no graph: $stem ($($process.ExitCode))" }
            $summary = @(Get-Content $graph | Where-Object { $_.StartsWith("summary`t") })
            if ($summary.Count -ne 1) { throw "Incomplete graph: $stem" }
            $records += [ordered]@{
                title=$title; variant=$variant; exit_code=$process.ExitCode; elapsed_ms=$timer.Elapsed.TotalMilliseconds
                input_sha256=(Get-FileHash $modulePath -Algorithm SHA256).Hash.ToLower()
                config_sha256=(Get-FileHash $config -Algorithm SHA256).Hash.ToLower()
                graph_sha256=(Get-FileHash $graph -Algorithm SHA256).Hash.ToLower(); summary=$summary[0]
            }
            Write-Output "$stem : $($summary[0]) ($($process.ExitCode))"
        } finally {
            if ($process.Id -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
            $process.Dispose()
        }
    }
}
$identity.records = $records
$identity | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 (Join-Path $outputPath 'measurement.json')
