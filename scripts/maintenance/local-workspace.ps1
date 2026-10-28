#requires -Version 7.0
# Inventory is the default. -Clean remains a dry-run alias for older callers.
# Applying requires an explicit report containing the reviewed plan; no migration.
param(
    [string]$Workspace = (Join-Path $PSScriptRoot '../..'),
    [string]$ReportDirectory = '',
    [string]$PythonExecutable = 'python',
    [double]$HashGiB = 64,
    [int]$TimeoutSeconds = 1200,
    [switch]$Apply,
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
$workspaceRoot = (Resolve-Path -LiteralPath $Workspace).Path
if ($Apply -and -not $ReportDirectory) { throw '-Apply requires -ReportDirectory from a reviewed dry-run.' }
if (-not $ReportDirectory) {
    $ReportDirectory = Join-Path $workspaceRoot ('.local/evidence/maintenance/' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fffffff'))
}
$arguments = @((Join-Path $PSScriptRoot 'workspace_audit.py'), '--workspace', $workspaceRoot,
    '--report', [IO.Path]::GetFullPath($ReportDirectory), '--hash-gib', $HashGiB, '--seconds', $TimeoutSeconds)
if ($Apply) { $arguments += '--execute' }
& $PythonExecutable @arguments
if ($LASTEXITCODE -ne 0) { throw "Workspace maintenance failed ($LASTEXITCODE); inspect the report/journal before retrying." }
