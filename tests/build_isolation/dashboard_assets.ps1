param()
$ErrorActionPreference = 'Stop'
$env:PSModulePath = Join-Path $PSHOME 'Modules'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$scratch = Join-Path $root ('.local/tmp/assets-cmake-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $scratch | Out-Null
$module = Join-Path $scratch 'vk.xex'
$header = Join-Path $scratch 'assets.h'
Set-Content -LiteralPath $module -Value '17559 fixture'
$hash = (Get-FileHash -LiteralPath $module).Hash.ToLowerInvariant()
[IO.File]::WriteAllText($header, '  {"vk.xex", "' + $hash + '"},' + "`n")
function Check([bool]$Success, [string]$Message) {
    $ErrorActionPreference = 'Continue' # expected native stderr on rejection (Windows PowerShell 5)
    $output = & cmake "-DROOT=$scratch" "-DHEADER=$header" '-DVERSION=17559' -P "$root/cmake/runtime/x360n_dashboard_assets.cmake" 2>&1 | Out-String
    $ErrorActionPreference = 'Stop'
    if (($LASTEXITCODE -eq 0) -ne $Success -or (!$Success -and !($output -replace '\s+', ' ').Contains($Message))) {
        throw "Unexpected build guard result: $output"
    }
}
Check $true ''
Set-Content -LiteralPath $module -Value '13604 fixture'
Check $false 'incompatible vk.xex'
Remove-Item -LiteralPath $module
Check $false 'required original dependency vk.xex is missing'
[IO.File]::WriteAllText($header, '  {"XenonJKLatin.xtt", ""},' + "`n")
Check $false 'no verified matching version/hash'
Remove-Item -LiteralPath $header
Remove-Item -LiteralPath $scratch
Write-Output 'PASS: original asset build gate accepts exact bytes and rejects wrong, absent and unidentified dependencies.'
