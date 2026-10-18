param(
  [Parameter(Mandatory=$true)][string]$Executable,
  [Parameter(Mandatory=$true)][string]$OutputDirectory,
  [int]$Samples = 2,
  [string[]]$Titles = @('dash_17559','g0a4c6253b34297dc971555c160fb8affde66f554296bd83bae609a48d5f5b47','motocross_madness','castle_crashers')
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$exe = [IO.Path]::GetFullPath($Executable)
$out = [IO.Path]::GetFullPath($OutputDirectory)
if ($Samples -lt 1 -or $Samples -gt 8) { throw 'Samples must be between 1 and 8' }
New-Item -ItemType Directory -Force $out | Out-Null
foreach ($title in $Titles) {
  for ($sample = 1; $sample -le $Samples; ++$sample) {
    $file = Join-Path $out "$title-$sample.json"
    if (Test-Path -LiteralPath $file) { throw "Refusing to overwrite $file" }
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $exe
    $info.Arguments = '"' + $root + '" ' + $title + ' shared'
    $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $info
    $lease = $null
    try {
      $clock = [Diagnostics.Stopwatch]::StartNew()
      $lease = [IO.File]::Open((Join-Path $root '.local/environment.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
      $lockMs = $clock.Elapsed.TotalMilliseconds
      [void]$process.Start()
      $stdout = $process.StandardOutput.ReadToEndAsync()
      $stderr = $process.StandardError.ReadToEndAsync()
      if (!$process.WaitForExit(60000)) { $process.Kill(); throw "Preflight deadline: $title" }
      $stdout.Result | Set-Content -LiteralPath $file -Encoding utf8
      $stderr.Result | Set-Content -LiteralPath "$file.err" -Encoding utf8
      if ($process.ExitCode -ne 0) { throw "Preflight failed: $title; $($stderr.Result)" }
      $result = $stdout.Result | ConvertFrom-Json
      @{environment_lock_ms=$lockMs;runner_ms=$clock.Elapsed.TotalMilliseconds;exit_code=$process.ExitCode} |
        ConvertTo-Json | Set-Content -LiteralPath "$file.runner.json" -Encoding utf8
      Write-Output "$title sample=$sample total_ms=$($result.total_ms) reads=$($result.read_bytes)"
    } finally {
      if (!$process.HasExited) { $process.Kill(); [void]$process.WaitForExit(3000) }
      $process.Dispose()
      if ($lease) { $lease.Dispose() }
    }
  }
}
