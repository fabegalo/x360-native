param([string]$Root = (Resolve-Path "$PSScriptRoot/../..").Path)
$ErrorActionPreference = 'Stop'
. "$Root/scripts/build/build-lease.ps1"
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('x360n-leases-' + [Guid]::NewGuid().ToString('N'))
function Assert($value, $message) { if (-not $value) { throw $message } }
function Release($lease) { foreach ($handle in $lease) { $handle.Dispose() } }
[void][IO.Directory]::CreateDirectory("$scratch/a")
[void][IO.Directory]::CreateDirectory("$scratch/b")
$a = $null; $b = $null
try {
    $a = Enter-BuildLease $scratch "$scratch/a" 'title_a'
    $b = Enter-BuildLease $scratch "$scratch/b" 'title_b'
    Release $b; $b = $null
    $rejected = $false
    try { $b = Enter-BuildLease $scratch "$scratch/a" 'title_b' } catch { $rejected = $true }
    Assert $rejected 'same build accepted another producer'
    $rejected = $false
    try { $b = Enter-BuildLease $scratch "$scratch/b" 'title_a' } catch { $rejected = $true }
    Assert $rejected 'same title accepted codegen in another build'
    $rejected = $false
    try { $b = [IO.File]::Open("$scratch/.local/environment.lock", 'OpenOrCreate', 'ReadWrite', 'None') } catch { $rejected = $true }
    Assert $rejected 'session acquired environment during build'
    Release $a; $a = $null
    $b = [IO.File]::Open("$scratch/.local/environment.lock", 'OpenOrCreate', 'ReadWrite', 'None')
    $rejected = $false
    try { $a = Enter-BuildLease $scratch "$scratch/a" 'title_a' } catch { $rejected = $true }
    Assert $rejected 'build acquired environment during session'
    $b.Dispose(); $b = $null
    $a = Enter-BuildLease $scratch "$scratch/a" 'title_a'
    Write-Output 'PASS: independent builds, same build/title exclusion, session exclusion and release'
} finally {
    if ($a) { Release $a }; if ($b) { Release $b }
    # Only this test's UUID directory is removed.
    Remove-Item -LiteralPath $scratch -Recurse -Force
}
