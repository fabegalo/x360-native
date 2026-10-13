# Regression check for the functional reference (rexglue/demo-iruka, XDK sample
# "Dolphin"; see docs/validation-iruka.md). Launches the recompiled title under
# `x360n run`, captures its window twice and checks, as separate levels:
#   L1 process started            report: launched
#   L2 XEX loaded, guest started  rex.log: "Function table initialized for module"
#   L3 ran without failing        ended by the supervisor timeout; no [critical]/[error]; 0 stub lines
#   L4 rendered                   capture is not a flat image (>= 32 quantized colors)
#   L5 animated                   two captures differ (mean abs diff >= 0.5)
# Exit codes: 0 all levels pass, 1 a level failed, 77 prerequisites missing (skip).
param(
    [string]$BuildDir = '.local/build/titles/iruka',
    [string]$Supervisor = '.local/build/win-amd64/src/supervisor/Debug/x360n.exe',
    [int]$Seconds = 20
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Split-Path -Parent $PSScriptRoot)).Path
Set-Location $root

$xexSha = '66ec34f2f558fe4e419d926108bf458a5c634ea8b5f76771b48ba633ecb61e31'
$gameRoot = Join-Path $root '.local\inputs\titles\iruka\extracted'
$x360n = Join-Path $root $Supervisor
$exe = Join-Path (Join-Path $root $BuildDir) 'iruka.exe'

foreach ($req in @($x360n, $exe, (Join-Path $gameRoot 'default.xex'))) {
    if (-not (Test-Path $req)) { Write-Host "SKIP: missing $req"; exit 77 }
}
$actual = (Get-FileHash -Algorithm SHA256 (Join-Path $gameRoot 'default.xex')).Hash.ToLowerInvariant()
if ($actual -ne $xexSha) { Write-Host "FAIL: default.xex SHA-256 $actual, expected $xexSha"; exit 1 }

$tmp = Join-Path $root ".local\tmp\regress-iruka-$PID"
New-Item -ItemType Directory -Force $tmp | Out-Null
$userRoot = Join-Path $root '.local\user\iruka-regress'
New-Item -ItemType Directory -Force $userRoot | Out-Null

$shotA = Join-Path $tmp 'frame-a.png'; $shotB = Join-Path $tmp 'frame-b.png'
$capture = Start-Process powershell -WindowStyle Hidden -PassThru -ArgumentList @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command',
    "Start-Sleep 8; & '$root\scripts\capture-window.ps1' -ProcessName iruka -Out '$shotA' -WaitSeconds 5; Start-Sleep 5; & '$root\scripts\capture-window.ps1' -ProcessName iruka -Out '$shotB' -WaitSeconds 3")

& $x360n run $exe --label regress-iruka --timeout $Seconds --rex-log -- `
    --gpu_plugin=xenos "--game_data_root=$gameRoot" "--user_data_root=$userRoot" | Out-Host
$runExit = $LASTEXITCODE
[void]$capture.WaitForExit(30000)

$runDir = Get-ChildItem (Join-Path $root '.local\runs') -Directory -Filter '*-regress-iruka' |
    Sort-Object Name | Select-Object -Last 1
$report = Get-Content -Raw (Join-Path $runDir.FullName 'report.json') | ConvertFrom-Json
$log = Get-Content -Raw (Join-Path $runDir.FullName 'rex.log') -ErrorAction SilentlyContinue
if (-not $log) { $log = '' }

$results = [ordered]@{}
$results['L1 process started'] = [bool]$report.result.launched
$results['L2 XEX loaded, guest started'] = $log.Contains('Function table initialized for module')
$badLines = @($log -split "`n" | Where-Object { $_ -match '\] \[(critical|error)\]' })
$results['L3 ran without failing'] = ($runExit -eq 2) -and $report.result.timed_out -and
    ($badLines.Count -eq 0) -and ($report.diagnostics.stub_lines -eq 0)

$distinct = 0; $diff = 0.0
if ((Test-Path $shotA) -and (Test-Path $shotB)) {
    $stats = & "$root\scripts\image-stats.ps1" -A $shotA -B $shotB
    $distinct = [int](($stats | Where-Object { $_ -like 'distinct_colors=*' }) -replace '.*=', '')
    $diff = [double]::Parse((($stats | Where-Object { $_ -like 'mean_abs_diff=*' }) -replace '.*=', ''),
        [Globalization.CultureInfo]::InvariantCulture)
    Copy-Item $shotA, $shotB $runDir.FullName
}
$results['L4 rendered'] = $distinct -ge 32
$results['L5 animated'] = $diff -ge 0.5

$failed = 0
foreach ($k in $results.Keys) {
    $ok = $results[$k]
    if (-not $ok) { $failed++ }
    Write-Host ("{0} {1}" -f ($(if ($ok) { 'PASS' } else { 'FAIL' })), $k)
}
Write-Host ("x360n exit={0}; errors/critical lines={1}; stub lines={2}; distinct colors={3}; frame diff={4}" -f `
    $runExit, $badLines.Count, $report.diagnostics.stub_lines, $distinct,
    $diff.ToString('F3', [Globalization.CultureInfo]::InvariantCulture))
foreach ($l in $badLines) { Write-Host "  $l" }

$summary = [ordered]@{ schema_version = 1; xex_sha256 = $actual; exe = $exe; levels = $results;
    x360n_exit = $runExit; error_lines = $badLines.Count; distinct_colors = $distinct; frame_diff = $diff }
$summary | ConvertTo-Json -Depth 4 | Out-File -Encoding utf8 (Join-Path $runDir.FullName 'regress.json')
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
Write-Host "run dir: $($runDir.FullName)"
if ($failed) { exit 1 } else { exit 0 }
