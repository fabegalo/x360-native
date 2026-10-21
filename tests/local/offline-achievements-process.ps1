param(
    [Parameter(Mandatory)] [string]$Executable,
    [string]$Output
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $Output) { $Output = Join-Path $root ('.local/evidence/runs/offline-achievements-' + [Guid]::NewGuid().ToString('N')) }
$Output = [IO.Path]::GetFullPath($Output)
if (Test-Path -LiteralPath $Output) { throw 'Use a fresh output directory; existing account data is never replaced.' }
[void][IO.Directory]::CreateDirectory($Output)
$store = Join-Path $Output 'store'
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$previousStore = $env:X360N_ACHIEVEMENT_TEST_STORE
$previousPhase = $env:X360N_ACHIEVEMENT_TEST_PHASE
$report = @{ executable = $Executable; phases = @(); artifacts = @() }
function Snapshot {
    @(Get-ChildItem -LiteralPath $store -File -Recurse | Sort-Object FullName | ForEach-Object {
        @{ path = $_.FullName.Substring($store.Length + 1); bytes = $_.Length;
           sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
}
try {
    $env:X360N_ACHIEVEMENT_TEST_STORE = $store
    foreach ($phase in @('write','read')) {
        $env:X360N_ACHIEVEMENT_TEST_PHASE = $phase
        $process = $null
        $timer = [Diagnostics.Stopwatch]::StartNew()
        try {
            $process = Start-Process -FilePath $Executable -WindowStyle Hidden -PassThru `
                -WorkingDirectory (Split-Path $Executable) `
                -ArgumentList '"Offline achievements survive independent write and read processes" --reporter compact' `
                -RedirectStandardOutput (Join-Path $Output "$phase.log") `
                -RedirectStandardError (Join-Path $Output "$phase.stderr.log")
            $retainedHandle = $process.Handle
            if (-not $process.WaitForExit(20000)) { throw "$phase exceeded 20 seconds" }
            $process.Refresh()
            if ($process.ExitCode -ne 0) { throw "$phase failed: exit $($process.ExitCode); see $Output" }
            $report.phases += @{ phase = $phase; pid = $process.Id; exit_code = $process.ExitCode;
                duration_ms = $timer.ElapsedMilliseconds; files = @(Snapshot) }
        } finally {
            if ($process -and -not $process.HasExited) {
                Stop-Process -Id $process.Id -Force
                [void]$process.WaitForExit(5000)
            }
        }
    }
    $before = @($report.phases[0].files | ForEach-Object { $_.path + ':' + $_.sha256 })
    $after = @($report.phases[1].files | ForEach-Object { $_.path + ':' + $_.sha256 })
    if (@(Compare-Object $before $after).Count) { throw 'Read-only restart changed persisted achievement files.' }
    $report.artifacts = @(@($Executable) + @(Get-ChildItem (Split-Path $Executable) -Filter '*.dll' -File | ForEach-Object FullName) | ForEach-Object {
        @{path = $_; sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Output 'report.json') -Encoding UTF8
    Write-Output "PASS: independent write/read processes, two profiles and two games; evidence: $Output"
} finally {
    $env:X360N_ACHIEVEMENT_TEST_STORE = $previousStore
    $env:X360N_ACHIEVEMENT_TEST_PHASE = $previousPhase
}
