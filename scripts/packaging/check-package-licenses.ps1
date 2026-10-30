# Offline release-candidate gate. Reuses package/payload indices; no preparation fingerprint.
param([Parameter(Mandatory)][string]$PackageDirectory,
      [Parameter(Mandatory)][string]$SdkDirectory)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../build/release-configuration.ps1')
. (Join-Path $PSScriptRoot 'lgpl-sources.ps1')
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$sdk = (Resolve-Path -LiteralPath $SdkDirectory).Path
function Digest($path) {
    if(-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing required file: $path" }
    (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Under($base, $relative) {
    if ($relative -match '(^/|\\|:|(^|/)\.\.?(/|$))') { throw 'Unsafe inventory path' }
    $p = [IO.Path]::GetFullPath((Join-Path $base $relative))
    if (-not $p.StartsWith($base + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Inventory path escapes package' }
    return $p
}
function VerifyIndex($base, $name) {
    $seen = @{}
    foreach ($line in [IO.File]::ReadAllLines((Join-Path $base $name))) {
        if ($line -notmatch '^([a-f0-9]{64})\t(.+)$') { throw "Malformed $name" }
        $hash=$Matches[1]; $rel=$Matches[2]
        if ($seen.ContainsKey($rel)) { throw "Duplicate index entry: $rel" }
        $seen[$rel]=$true
        if ((Digest (Under $base $rel)) -ne $hash) { throw "Index mismatch: $rel" }
    }
    foreach($f in Get-ChildItem -LiteralPath $base -Recurse -File) {
        $r=$f.FullName.Substring($base.Length+1).Replace('\','/')
        if ($r -ne $name -and -not $seen.ContainsKey($r)) { throw "Unindexed package entry: $r" }
    }
}
$payload=Join-Path $package 'app/workspace'
$inventory=Get-Content -LiteralPath (Join-Path $package 'licenses/inventory.json') -Raw | ConvertFrom-Json
$metadata=Get-Content -LiteralPath (Join-Path $package 'package.json') -Raw | ConvertFrom-Json
$lock=Get-Content -LiteralPath (Join-Path $payload 'deps.lock.json') -Raw | ConvertFrom-Json
if($inventory.schema -ne 1 -or $metadata.sdk_commit -ne $inventory.sdk_commit -or $lock.rexglue_sdk.commit -ne $inventory.sdk_commit) { throw 'License inventory SDK revision mismatch' }
if(-not $metadata.desktop_only) { throw 'License scope only covers the desktop distribution' }
# Binaries, compiled units and link inputs differ per system; notices are shared.
$platform=if($metadata.platform) { [string]$metadata.platform } else { 'windows-x64' }
$scope=$inventory.platforms.$platform
if(-not $scope) { throw "No license inventory for platform: $platform" }
if((Digest (Join-Path $package 'licenses/inventory.json')) -ne (Digest (Join-Path $payload 'licenses/inventory.json'))) { throw 'Payload/license inventory mismatch' }
if((Digest (Join-Path $package 'licenses/excluded-loader-constants.json')) -ne (Digest (Join-Path $payload 'licenses/excluded-loader-constants.json'))) { throw 'Payload/exclusion policy mismatch' }
if((Digest (Join-Path $payload 'payload.sha256')) -ne $metadata.payload_sha256) { throw 'Payload identity mismatch' }
foreach($required in @('LICENSE','THIRD_PARTY_NOTICES.md')) {
    if((Digest (Join-Path $package $required)) -ne (Digest (Join-Path $payload $required))) { throw "Notice differs from committed payload: $required" }
}
$expected=@('LICENSE','THIRD_PARTY_NOTICES.md','READ-ME.txt','package.json','package.sha256','app/compile-commands.json','app/dependency-inputs.json','app/link-inputs.json','licenses/inventory.json','licenses/rexglue-attributions.txt','licenses/header-attributions.txt') + @($scope.executables)
$ids=@{}
$expected += 'licenses/excluded-loader-constants.json'
$expected += 'licenses/relinking.txt'
$expected += @('licenses/libmspack-CMakeLists.txt','licenses/sources/libmspack/CMakeLists.txt')
if ((Digest (Join-Path $package 'licenses/sources/libmspack/CMakeLists.txt')) -ne
    (Digest (Join-Path $payload 'licenses/libmspack-CMakeLists.txt'))) { throw 'Decoder rebuild script differs' }
$expected += @(Assert-LgplSources $inventory $package $payload $sdk)
foreach($component in $inventory.components) {
    if($ids.ContainsKey($component.id)) { throw 'Duplicate component identity' }; $ids[$component.id]=$true
    if($component.scope -like 'binary*' -and $component.spdx -eq 'NOASSERTION') { throw "Unidentified desktop component: $($component.id)" }
    if($component.license_file) {
        $expected += $component.license_file
        foreach($base in @($package,$payload)) {
            if((Digest (Under $base $component.license_file)) -ne $component.license_sha256) { throw "Missing or changed original license: $($component.id)" }
        }
        # Compare original legal texts in the freshly pinned SDK snapshot where present.
        if($component.original -and ($component.id -eq 'rexglue-sdk' -or $component.original -like 'thirdparty/*')) {
            $original=Under $sdk $component.original
            if(Test-Path -LiteralPath $original) {
                if((Digest $original) -ne $component.license_sha256) { throw "Upstream license changed: $($component.id)" }
            }
        }
    }
    foreach($evidence in $component.source_evidence) {
        if((Digest (Under $sdk $evidence.sdk_path)) -ne $evidence.sha256) { throw "SDK provenance source changed: $($component.id)" }
    }
}
if((Digest (Join-Path $package 'licenses/rexglue-attributions.txt')) -ne $inventory.attributions_sha256) { throw 'Attribution text changed' }
$sdkAttributions=Get-Content (Join-Path $package 'licenses/rexglue-attributions.txt') -Raw
foreach($unit in @($scope.sdk_translation_units) + @($scope.sdk_private_headers)) {
    if(-not $sdkAttributions.Contains("--- $unit ---")) { throw 'SDK file attribution missing' }
}
foreach($id in @('nlohmann-json') + @($scope.runtime_components) + @($scope.vendored_dependencies.PSObject.Properties | ForEach-Object { $_.Value })) { if(-not $ids.ContainsKey($id)) { throw 'Missing embedded component identity' } }
if(-not $ids.ContainsKey('miniz') -or -not $ids.ContainsKey('libmspack')) { throw 'Missing extraction decoder identity' }
if((Digest (Join-Path $package 'licenses/header-attributions.txt')) -ne $inventory.header_attributions_sha256) { throw 'Header attribution text changed' }
$icon=@($inventory.components | Where-Object id -eq 'desktop-icon')
if($icon.Count -ne 1 -or $icon[0].spdx -ne 'BSD-3-Clause' -or
   (Digest (Join-Path $payload 'src/desktop/resources/desktop.ico')) -ne $icon[0].content_sha256) { throw 'Desktop icon provenance mismatch' }
if(-not (Test-Path -LiteralPath (Join-Path $payload 'docs/reports/maintenance/ICON_PROVENANCE.md'))) { throw 'Missing desktop icon provenance' }
& (Join-Path $PSScriptRoot 'generate-desktop-icon.ps1') -OutputPath (Join-Path $payload 'src/desktop/resources/desktop.ico') -Check
# Supplemental upstream texts travel with the exported source/environment too.
foreach($id in @('tiny-aes-c','disruptorplus','dxc-headers','dxbc-md5','sha256','disasm')) {
    $record=@($inventory.components | Where-Object id -eq $id)
    if($record.Count -ne 1 -or -not $record[0].license_file) { throw "Missing supplemental SDK license: $id" }
}
$linkInputs=Get-Content (Join-Path $package 'app/link-inputs.json') -Raw | ConvertFrom-Json
if(Compare-Object @($scope.link_libraries | Sort-Object) @($linkInputs | Sort-Object -Unique)) { throw 'Unreviewed static link dependency' }
foreach($dep in $metadata.dependencies.PSObject.Properties) {
    if($dep.Name -notin $inventory.desktop_dependencies) { throw "Unidentified build dependency: $($dep.Name)" }
    $record=@($inventory.components | Where-Object id -eq $dep.Name)
    if($record.Count -ne 1 -or $record[0].revision -ne $dep.Value) { throw "Dependency revision differs from license inventory: $($dep.Name)" }
}
if(@($metadata.dependencies.PSObject.Properties).Count -ne @($inventory.desktop_dependencies).Count) { throw 'Missing build dependency identity' }
$units=@(); $thirdpartyUnits=@(); $generatedUnits=@()
$compileCommands = @(Get-Content (Join-Path $package 'app/compile-commands.json') -Raw | ConvertFrom-Json | ForEach-Object { $_ })
Assert-ReleaseCompileCommands $compileCommands
foreach($command in $compileCommands) {
    $f=$command.file.Replace('\','/')
    if($f -match '^<source>/external/rexglue-sdk/(src/.+)$') { $units += $Matches[1] }
    elseif($f -match '^<source>/external/rexglue-sdk/thirdparty/([^/]+)/') {
        if ($f.Substring('<source>/external/rexglue-sdk/'.Length) -in $scope.thirdparty_translation_units) {
            $thirdpartyUnits += $f.Substring('<source>/external/rexglue-sdk/'.Length)
        } elseif($Matches[1] -notin @('fmt','spdlog','catch2')) { throw 'Unreviewed third-party translation unit' }
    } elseif($f -match '^<build>/(.+)$') {
        # Build-generated units: SDL's precompiled header and the launcher's embedded fonts.
        if($Matches[1] -notin $scope.generated_translation_units) { throw 'Unreviewed generated translation unit' }
        $generatedUnits += $Matches[1]
    } elseif($f -notmatch '^<source>/(src/(desktop|supervisor|content|platform)/|tests/(supervisor|content|desktop)/)') { throw 'Unreviewed distribution translation unit' }
}
if(Compare-Object @($scope.sdk_translation_units | Sort-Object) @($units | Sort-Object -Unique)) { throw 'SDK extraction source scope changed' }
if(Compare-Object @($scope.thirdparty_translation_units | Sort-Object) @($thirdpartyUnits | Sort-Object -Unique)) { throw 'Third-party source scope changed' }
if(Compare-Object @($scope.generated_translation_units | Sort-Object) @($generatedUnits | Sort-Object -Unique)) { throw 'Generated source scope changed' }
$dependencyInputs=Get-Content (Join-Path $package 'app/dependency-inputs.json') -Raw | ConvertFrom-Json
foreach($dependencyInput in $dependencyInputs) {
    if($dependencyInput -notin $scope.sdk_inputs) { throw 'Unreviewed transitive header dependency' }
    if($dependencyInput -match '^thirdparty/([^/]+)/') {
        # In-tree SDK sources compiled on this platform only, each mapped to its license record.
        if($Matches[1] -notin $inventory.desktop_dependencies -and -not $scope.vendored_dependencies.($Matches[1])) { throw 'Unreviewed transitive header dependency' }
        if($dependencyInput -like 'thirdparty/inja/*' -and $dependencyInput -notlike 'thirdparty/inja/third_party/include/nlohmann/*') { throw 'Unreviewed inja nested dependency' }
    } elseif($dependencyInput -notlike 'include/rex/*' -and $dependencyInput -notin @($scope.sdk_private_headers) -and $dependencyInput -notin $scope.sdk_translation_units) { throw 'Unreviewed SDK input' }
}
. (Join-Path $PSScriptRoot 'excluded-constants.ps1')
$policy = Get-Content (Join-Path $PSScriptRoot '../../licenses/excluded-loader-constants.json') -Raw | ConvertFrom-Json
if ($policy.schema -ne 1 -or $policy.rules.Count -lt 3) { throw 'SDK constant exclusion evidence missing' }
foreach($file in Get-ChildItem -LiteralPath $package -Recurse -File) {
    $rel=$file.FullName.Substring($package.Length+1).Replace('\','/')
    if($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Reparse point in package' }
    if($rel -notlike 'app/workspace/*' -and $rel -notin $expected) { throw "Unidentified distributed file: $rel" }
    if($rel -match '\.(xex|xexp|xtt|ttf|otf|stfs|iso|nand|key|pem|pfx|bin)$' -or $rel -match '(^|/)(\.git|\.local|external)/' -or ($rel -match '/generated/' -and $rel -notmatch '/generated/rexglue\.cmake$')) { throw "Forbidden distribution content: $rel" }
    if($rel -match '\.(exe|dll|lib|pdb|so|dylib|a|o)$' -and $rel -notin $scope.executables) { throw "Unidentified binary: $rel" }
    $bytes=[IO.File]::ReadAllBytes($file.FullName)
    $magic=[Text.Encoding]::ASCII.GetString($bytes,0,[Math]::Min(4,$bytes.Length))
    # Mach-O 64-bit (CF FA ED FE) and universal (CA FE BA BE) images, like PE and ELF.
    $machO=$bytes.Length -ge 4 -and (($bytes[0] -eq 0xCF -and $bytes[1] -eq 0xFA -and $bytes[2] -eq 0xED -and $bytes[3] -eq 0xFE) -or
        ($bytes[0] -eq 0xCA -and $bytes[1] -eq 0xFE -and $bytes[2] -eq 0xBA -and $bytes[3] -eq 0xBE))
    if($magic -in @('XEX1','XEX2','CON ','LIVE','PIRS') -or (($magic.StartsWith('MZ') -or $magic -eq ([string][char]0x7f + 'ELF') -or $machO) -and $rel -notin $scope.executables)) { throw "Embedded executable/container: $rel" }
    $textFile = $rel -match '\.(cpp|h|hpp|c|patch|json|toml|ps1|md|txt)$'
    if (Test-ExcludedConstant $bytes $policy.rules -Text:$textFile) { throw "Sensitive loader constant in: $rel" }
    if($rel -match '\.(cpp|h|hpp|c|patch|json|toml|ps1|md|txt)$') {
        $text=[Text.Encoding]::UTF8.GetString($bytes)
        if($text -match '-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----') { throw "Private key material in: $rel" }
    }
}
foreach($exe in $scope.executables) { if(-not (Test-Path -LiteralPath (Under $package $exe))) { throw 'Missing inventoried executable' } }
VerifyIndex $payload 'payload.sha256'
if(Test-Path (Join-Path $package 'package.sha256')) { VerifyIndex $package 'package.sha256' }
Write-Output "License/content gate passed: $($inventory.components.Count) components; $(@($scope.executables).Count) $platform executables; original notices and existing payload identity verified. Publication approval remains separate."
