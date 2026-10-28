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
    [switch]$RefreshPlan,
    [switch]$MeasureAllocation,
    [string[]]$RetireTrial = @(),
    [string]$InventoryReport = '',
    [string[]]$ReviewedProcessImage = @(),
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
if ($RefreshPlan) {
    if ($Apply) { throw 'RefreshPlan and Apply are separate steps.' }
    $arguments += '--refresh-plan'
}
if ($MeasureAllocation) { $arguments += '--measure-allocation' }
if ($RetireTrial.Count) {
    if ($Apply -or $RefreshPlan -or $MeasureAllocation) { throw 'Trial retirement planning is a separate explicit step.' }
    foreach ($trialPath in $RetireTrial) { $arguments += @('--retire-trial',$trialPath) }
    if ($InventoryReport) { $arguments += @('--inventory-report',$InventoryReport) }
    foreach ($imagePath in $ReviewedProcessImage) { $arguments += @('--review-process-image',$imagePath) }
}
& $PythonExecutable @arguments
if ($LASTEXITCODE -ne 0) { throw "Workspace maintenance failed ($LASTEXITCODE); inspect the report/journal before retrying." }
