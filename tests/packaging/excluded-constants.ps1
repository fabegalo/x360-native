param([string]$PackageDirectory = '')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
. (Join-Path $root 'scripts/packaging/excluded-constants.ps1')
$policy = Get-Content (Join-Path $root 'licenses/excluded-loader-constants.json') -Raw | ConvertFrom-Json
if (@('xe_xex1_retail_key','xe_xex1_devkit_key' | Where-Object { $_ -notin @($policy.rules.id) }).Count) {
    throw 'An unapproved key exclusion was removed'
}
if ('xe_xex2_retail_key' -in @($policy.rules.id) -or
    @($policy.permitted_public_constants).Count -ne 1 -or
    $policy.permitted_public_constants[0].id -ne 'xe_xex2_retail_key' -or
    $policy.permitted_public_constants[0].sha256 -ne 'a0bd3f5f56adf2b0238a2702c0c0cd8548fb34b3dc19345943dc011010723fe4') {
    throw 'The approved public-constant exception changed scope'
}
$value = [byte[]](101..116)
$sha = [Security.Cryptography.SHA256]::Create()
try { $hash = [BitConverter]::ToString($sha.ComputeHash($value)).Replace('-', '').ToLowerInvariant() } finally { $sha.Dispose() }
$rules = @(@{first_byte=$value[0]; sha256=$hash})
$tokens = @($value | ForEach-Object { '0x' + $_.ToString('x2') })
$line1 = ($tokens[0..7] -join ', ') + ','
$line2 = $tokens[8..15] -join ', '
$cases = @{
    'cpp' = "const auto synthetic[] = { $line1`n  $line2 };"
    'added-diff' = "--- a/test.cpp`n+++ b/test.cpp`n@@ -1 +1,2 @@`n+const auto synthetic[] = { $line1`n+  $line2 };"
    'removed-diff' = "-const auto synthetic[] = { $line1`n-  $line2 };"
    'mixed-diff' = " const auto synthetic[] = { $line1`n+  $line2 };"
    'hex' = ($value | ForEach-Object { $_.ToString('x2') }) -join ''
}
foreach ($name in $cases.Keys) {
    if (-not (Test-ExcludedConstant ([Text.Encoding]::UTF8.GetBytes($cases[$name])) $rules -Text)) { throw "Accepted synthetic constant: $name" }
    Write-Output "PASS rejected synthetic $name"
}
if (-not (Test-ExcludedConstant $value $rules)) { throw 'Accepted binary constant' }
if (Test-ExcludedConstant ([Text.Encoding]::UTF8.GetBytes('ordinary source + value')) $rules -Text) { throw 'Rejected ordinary source' }
if ($PackageDirectory) {
    $policy = Get-Content (Join-Path $root 'licenses/excluded-loader-constants.json') -Raw | ConvertFrom-Json
    foreach ($file in Get-ChildItem -LiteralPath $PackageDirectory -Recurse -File) {
        if (Test-ExcludedConstant ([IO.File]::ReadAllBytes($file.FullName)) $policy.rules -Text:($file.Extension -in @('.cpp','.h','.patch','.json','.ps1','.md','.txt'))) {
            throw "Excluded content in exported package: $($file.Name)"
        }
    }
    Write-Output 'PASS exported package content'
}
