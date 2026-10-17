param([Parameter(Mandatory)][string]$PackageDirectory,
      [Parameter(Mandatory)][string]$SdkDirectory)
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$gate=Join-Path $root 'scripts/packaging/check-package-licenses.ps1'
$temp=Join-Path $root ('.local/evidence/audit/license-tests-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $temp | Out-Null
function Index($base,$name) {
    $lines=@(Get-ChildItem $base -File -Recurse | Where-Object FullName -ne (Join-Path $base $name) | Sort-Object FullName | ForEach-Object {
        (Get-FileHash $_.FullName).Hash.ToLowerInvariant()+"`t"+$_.FullName.Substring($base.Length+1).Replace('\','/')
    })
    [IO.File]::WriteAllText((Join-Path $base $name),($lines -join "`n")+"`n",[Text.UTF8Encoding]::new($false))
}
function Refresh($p) {
    Index (Join-Path $p 'app/workspace') 'payload.sha256'
    $meta=Get-Content (Join-Path $p 'package.json') -Raw | ConvertFrom-Json
    $meta.payload_sha256=(Get-FileHash (Join-Path $p 'app/workspace/payload.sha256')).Hash.ToLowerInvariant()
    $meta | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $p 'package.json') -Encoding UTF8
    Index $p 'package.sha256'
}
& $gate -PackageDirectory $PackageDirectory -SdkDirectory $SdkDirectory
$cases=@(
 @{name='changed-icon'; expected='Desktop icon provenance mismatch'; mutate={param($p) [IO.File]::WriteAllBytes((Join-Path $p 'app/workspace/src/desktop/resources/desktop.ico'),[byte[]]@(0,0,1,0))}},
 @{name='unproven-icon'; expected='Desktop icon differs from original'; mutate={param($p) $path=Join-Path $p 'app/workspace/src/desktop/resources/desktop.ico'; $b=[IO.File]::ReadAllBytes($path); $b[$b.Length-1]=1; [IO.File]::WriteAllBytes($path,$b); foreach($base in @($p,(Join-Path $p 'app/workspace'))) { $i=Get-Content (Join-Path $base 'licenses/inventory.json') -Raw | ConvertFrom-Json; ($i.components | Where-Object id -eq 'desktop-icon').content_sha256=(Get-FileHash $path).Hash.ToLowerInvariant(); $i | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $base 'licenses/inventory.json') }}},
 @{name='missing-icon-provenance'; expected='Missing desktop icon provenance'; mutate={param($p) Remove-Item -LiteralPath (Join-Path $p 'app/workspace/docs/reports/maintenance/ICON_PROVENANCE.md')}},
 @{name='missing-sdk-license'; expected='Missing required file'; mutate={param($p) Remove-Item -LiteralPath (Join-Path $p 'licenses/dxc-headers.txt')}},
 @{name='removed-sdk-license-record'; expected='Missing supplemental SDK license'; mutate={param($p) foreach($base in @($p,(Join-Path $p 'app/workspace'))) { $i=Get-Content (Join-Path $base 'licenses/inventory.json') -Raw | ConvertFrom-Json; ($i.components | Where-Object id -eq 'dxc-headers').license_file=$null; $i | ConvertTo-Json -Depth 12 | Set-Content (Join-Path $base 'licenses/inventory.json') }}},
 @{name='missing-license'; expected='Missing required file'; mutate={param($p) Remove-Item -LiteralPath (Join-Path $p 'licenses/nlohmann-json.txt')}},
 @{name='changed-license'; expected='Missing or changed original license'; mutate={param($p) Add-Content (Join-Path $p 'licenses/fmt.txt') 'tamper'}},
 @{name='missing-attribution'; expected='SDK file attribution missing'; mutate={param($p) foreach($base in @($p,(Join-Path $p 'app/workspace'))) { Set-Content (Join-Path $base 'licenses/rexglue-attributions.txt') 'empty'; $i=Get-Content (Join-Path $base 'licenses/inventory.json') -Raw | ConvertFrom-Json; $i.attributions_sha256=(Get-FileHash (Join-Path $base 'licenses/rexglue-attributions.txt')).Hash.ToLowerInvariant(); $i | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $base 'licenses/inventory.json') }}},
 @{name='foreign-dll'; expected='Unidentified distributed file'; mutate={param($p) [IO.File]::WriteAllBytes((Join-Path $p 'foreign.dll'),[byte[]](77,90,0,0))}},
 @{name='extra-exe'; expected='Unidentified distributed file'; mutate={param($p) Copy-Item (Join-Path $p 'app/x360n.exe') (Join-Path $p 'surprise.exe')}},
 @{name='missing-exe'; expected='Missing inventoried executable'; mutate={param($p) Remove-Item -LiteralPath (Join-Path $p 'app/x360n-extract.exe')}},
 @{name='renamed-xex'; expected='Embedded executable/container'; mutate={param($p) [IO.File]::WriteAllText((Join-Path $p 'app/workspace/renamed.dat'),'XEX2synthetic-test')}},
 @{name='proprietary-font'; expected='Forbidden distribution content'; mutate={param($p) Set-Content (Join-Path $p 'app/workspace/font.xtt') 'synthetic'}},
 @{name='generated-aot'; expected='Forbidden distribution content'; mutate={param($p) Set-Content (Join-Path $p 'app/workspace/titles/iruka/generated/foreign.cpp') '// synthetic AOT marker'}},
 @{name='header-scope'; expected='Unreviewed transitive header'; mutate={param($p) '["thirdparty/disasm/ppc.h"]' | Set-Content (Join-Path $p 'app/dependency-inputs.json')}},
 @{name='static-link-scope'; expected='Unreviewed static link dependency'; mutate={param($p) '["unidentified.lib"]' | Set-Content (Join-Path $p 'app/link-inputs.json')}},
 @{name='compiler-scope'; expected='Unreviewed third-party translation'; mutate={param($p) $c=Get-Content (Join-Path $p 'app/compile-commands.json') -Raw | ConvertFrom-Json; $c[0].file='<source>/external/rexglue-sdk/thirdparty/disasm/ppc-dis.c'; $c | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $p 'app/compile-commands.json')}},
 @{name='inventory-revision'; expected='Dependency revision differs'; mutate={param($p) $m=Get-Content (Join-Path $p 'package.json') -Raw | ConvertFrom-Json; $m.dependencies.fmt='incorrect'; $m | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $p 'package.json')}},
 @{name='unknown-component'; expected='Unidentified desktop component'; mutate={param($p) foreach($base in @($p,(Join-Path $p 'app/workspace'))) { $i=Get-Content (Join-Path $base 'licenses/inventory.json') -Raw | ConvertFrom-Json; ($i.components | Where-Object id -eq fmt).spdx='NOASSERTION'; $i | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $base 'licenses/inventory.json') }}},
 @{name='loader-constant'; expected='Sensitive loader constant'; mutate={param($p) $s=Get-Content (Join-Path $SdkDirectory 'src/system/xex_module.cpp') -Raw; $m=[regex]::Match($s,'(?s)static const uint8_t xe_xex2_retail_key\[16\]\s*=\s*\{([^}]+)\}'); $b=[byte[]]@([regex]::Matches($m.Groups[1].Value,'0x([0-9a-fA-F]{2})') | ForEach-Object { [Convert]::ToByte($_.Groups[1].Value,16) }); [IO.File]::WriteAllBytes((Join-Path $p 'app/workspace/constant.dat'),$b)}}
)
foreach($case in $cases) {
    $p=Join-Path $temp $case.name
    Copy-Item -LiteralPath $PackageDirectory -Destination $p -Recurse
    & $case.mutate $p
    # Even a self-consistent new file index must not bless forbidden content.
    Refresh $p
    $failed=$false
    try { & $gate -PackageDirectory $p -SdkDirectory $SdkDirectory | Out-Null }
    catch { if($_.Exception.Message -notmatch $case.expected) { throw "Wrong failure for $($case.name): $($_.Exception.Message)" }; $failed=$true }
    if(-not $failed) { throw "Gate accepted negative case: $($case.name)" }
    Write-Output "PASS rejected $($case.name)"
}
# Modification after index generation must also be rejected by existing integrity checks.
$p=Join-Path $temp 'substituted-executable'
Copy-Item -LiteralPath $PackageDirectory -Destination $p -Recurse
Refresh $p
Copy-Item (Join-Path $p 'app/x360n.exe') (Join-Path $p 'app/x360n-extract.exe') -Force
$rejected=$false
try { & $gate -PackageDirectory $p -SdkDirectory $SdkDirectory | Out-Null }
catch { if($_.Exception.Message -notmatch 'Index mismatch') { throw }; $rejected=$true }
if(-not $rejected) { throw 'Substituted executable accepted' }
Write-Output "PASS rejected substituted-executable; 1 positive + $($cases.Count+1) negative cases"
