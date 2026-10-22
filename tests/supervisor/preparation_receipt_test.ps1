param([string]$Root = (Resolve-Path "$PSScriptRoot/../..").Path, [string]$ReceiptScript)
$ErrorActionPreference = 'Stop'
$env:PSModulePath = Join-Path $PSHOME 'Modules'
if (-not $ReceiptScript) { $ReceiptScript = "$Root/scripts/build/preparation-receipt.ps1" }
. $ReceiptScript
if ([IO.Path]::GetFullPath($ReceiptScript) -eq [IO.Path]::GetFullPath("$Root/scripts/build/preparation-receipt.ps1")) {
    . "$Root/scripts/build/incremental-state.ps1"
}
$scratch = Join-Path ([IO.Path]::GetTempPath()) ("x360n-receipt-" + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($scratch)
function Assert($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
try {
    foreach ($size in @(0, 3, 1048575, 1048576, 1048577, 2097169)) {
        $vector = Join-Path $scratch 'hash-vector'
        $data = [byte[]]::new($size)
        for ($i = 0; $i -lt $size; $i += 4093) { $data[$i] = $i % 251 }
        [IO.File]::WriteAllBytes($vector, $data)
        $reference = [Security.Cryptography.SHA256]::Create()
        try { $expected = ([BitConverter]::ToString($reference.ComputeHash($data))).Replace('-', '').ToLowerInvariant() }
        finally { $reference.Dispose() }
        Assert ((Get-ReceiptHash $vector) -eq $expected) "SHA-256 streaming boundary differs at $size bytes"
        $entry = @(Get-ReceiptEntries @($vector))
        Assert ($entry.Count -eq 1 -and $entry[0].sha256 -eq $expected) "Compiled SHA-256 differs at $size bytes"
        Assert (Test-ReceiptEntries $entry) "Compiled receipt verification differs at $size bytes"
    }
    if (Test-Path Function:\Initialize-ProductFingerprints) {
        $originalHelper = $script:X360NProductFingerprintSource
        $changedHelper = Join-Path $scratch 'changed-helper.cs'
        [IO.File]::WriteAllText($changedHelper, [IO.File]::ReadAllText($originalHelper) + "`n// changed source identity")
        $rejected = $false
        try { $script:X360NProductFingerprintSource = $changedHelper; Initialize-ProductFingerprints }
        catch { $rejected = $_.Exception.Message -like '*start a new host*' }
        finally { $script:X360NProductFingerprintSource = $originalHelper }
        Assert $rejected 'stale compiled helper accepted in the same host'
        Assert (Test-ReceiptEntries $entry) 'restored helper identity rejected'
    }
    $rewritten = Join-Path $scratch 'producer-input.toml'
    [IO.File]::WriteAllText($rewritten, "[project]`nsdk_version = '0.10.0'`n")
    $metadata = @(Get-CodegenInputMetadata @($rewritten))
    $changedTime = $metadata[0].last_write_time_utc.AddSeconds(2)
    [IO.File]::WriteAllText($rewritten, [IO.File]::ReadAllText($rewritten))
    [IO.File]::SetLastWriteTimeUtc($rewritten, $changedTime)
    Restore-UnchangedCodegenInputMetadata $metadata
    Assert ([IO.File]::GetLastWriteTimeUtc($rewritten) -eq $metadata[0].last_write_time_utc) 'identical SDK rewrite invalidated CMake metadata'
    [IO.File]::WriteAllText($rewritten, "[project]`nsdk_version = 'changed'`n")
    [IO.File]::SetLastWriteTimeUtc($rewritten, $changedTime)
    Restore-UnchangedCodegenInputMetadata $metadata
    Assert ([IO.File]::GetLastWriteTimeUtc($rewritten) -eq $changedTime) 'changed SDK input had its timestamp restored'
    Assert (-not (Test-ReceiptEntries $metadata)) 'changed SDK input approved after metadata check'
    [IO.File]::Delete($rewritten)
    Restore-UnchangedCodegenInputMetadata $metadata
    $project = Join-Path $scratch 'project with spaces'
    $generated = Join-Path $project 'generated/default'
    $native = Join-Path $scratch 'native'
    [void][IO.Directory]::CreateDirectory($generated)
    [void][IO.Directory]::CreateDirectory($native)
    $dependency = Join-Path $scratch 'original.xex'
    $analysis = Join-Path $scratch 'analysis.toml'
    $manifest = Join-Path $project 'sample_manifest.toml'
    $output = Join-Path $generated 'emitted.cpp'
    $stamp = Join-Path $generated 'codegen.stamp'
    $depfile = Join-Path $generated 'codegen.d'
    foreach ($path in @($dependency, $analysis, $manifest, $output, $stamp)) { [IO.File]::WriteAllText($path, 'original') }
    $lines = @('output: \') + @($dependency, $analysis, $manifest | ForEach-Object { '  ' + $_.Replace('\', '/').Replace(' ', '\ ') + ' \' })
    [IO.File]::WriteAllLines($depfile, $lines)
    $previous = @{inputs=@(Get-ReceiptEntries @($dependency, $analysis, $manifest, $output, $stamp, $depfile))}
    Assert (Test-VerifiedCodegen $previous $project $native) 'unchanged AOT closure rejected'
    foreach ($path in @($dependency, $analysis, $manifest, $output, $stamp, $depfile)) {
        $originalBytes = [IO.File]::ReadAllBytes($path); $originalTime = [IO.File]::GetLastWriteTimeUtc($path)
        [IO.File]::WriteAllText($path, 'modified'); [IO.File]::SetLastWriteTimeUtc($path, $originalTime)
        Assert (-not (Test-VerifiedCodegen $previous $project $native)) "modified AOT closure accepted: $path"
        [IO.File]::WriteAllBytes($path, $originalBytes); [IO.File]::SetLastWriteTimeUtc($path, $originalTime)
    }
    [IO.File]::WriteAllText((Join-Path $project 'added_manifest.toml'), 'new module')
    Assert (-not (Test-VerifiedCodegen $previous $project $native)) 'new module manifest ignored'
    Assert (-not (Test-VerifiedCodegen @{inputs=@()} $project $native)) 'missing authenticated AOT inventory accepted'
    $inputFile = Join-Path $scratch 'input with spaces.xex'
    [IO.File]::WriteAllText($inputFile, 'version1')
    $entries = @(Get-ReceiptEntries @($inputFile))
    $second = Join-Path $scratch 'second'; [IO.File]::WriteAllText($second, 'second')
    $merged = @(Get-ReceiptEntries @($inputFile, $second, $inputFile) | Sort-Object path -Unique)
    Assert ($merged.Count -eq 2) 'deduplication lost distinct input hashes'
    Assert (Test-ReceiptEntries $entries) 'positive content check failed'
    $time = [IO.File]::GetLastWriteTimeUtc($inputFile)
    [IO.File]::WriteAllText($inputFile, 'version2')
    [IO.File]::SetLastWriteTimeUtc($inputFile, $time)
    Assert (-not (Test-ReceiptEntries $entries)) 'same-size same-mtime substitution accepted'
    $file = Join-Path $scratch 'receipt.json'
    Publish-PreparationReceipt $file @{schema=1; title='first'; inputs=$entries}
    Assert ((Get-Content -Raw $file | ConvertFrom-Json).title -eq 'first') 'initial publication failed'
    Publish-PreparationReceipt $file @{schema=1; title='second'; inputs=$entries}
    Assert ((Get-Content -Raw $file | ConvertFrom-Json).title -eq 'second') 'atomic replacement failed'
    $original = [IO.File]::ReadAllText($file)
    $locked = [IO.File]::Open($file, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None)
    $failed = $false
    try { Publish-PreparationReceipt $file @{schema=1; title='partial'} } catch { $failed = $true }
    finally { $locked.Dispose() }
    Assert $failed 'publication failure was ignored'
    Assert ([IO.File]::ReadAllText($file) -eq $original) 'failed publication changed the valid receipt'
    Assert (@(Get-ChildItem $scratch -Filter '*.tmp').Count -eq 0) 'temporary document leaked'
    Assert ((Resolve-BuildPath $scratch $inputFile) -eq $inputFile) 'absolute dependency path corrupted'
    $templates = Join-Path $scratch 'templates'; [void][IO.Directory]::CreateDirectory($templates)
    $glob = @{ directory=$templates; pattern='*.inja'; recursive=$true; directories=$false; files=@() }
    Assert (Test-ReceiptGlobs @($glob)) 'empty source inventory rejected'
    [IO.File]::WriteAllText((Join-Path $templates 'notes.txt'), 'unrelated')
    Assert (Test-ReceiptGlobs @($glob)) 'unrelated file invalidated template inventory'
    [IO.File]::WriteAllText((Join-Path $templates 'new.inja'), 'new template')
    Assert (-not (Test-ReceiptGlobs @($glob))) 'new template missing from graph was accepted'
    $build = Join-Path $scratch 'build'; [void][IO.Directory]::CreateDirectory($build)
    Assert-AotBuildDirectory $scratch $build ''
    Assert-AotBuildDirectory $scratch $build 'build'
    Assert-AotBuildDirectory $scratch $build $build
    $failed = $false
    try { Assert-AotBuildDirectory $scratch $build 'another' } catch { $failed = $true }
    Assert $failed 'generated codegen output accepted an alternate native build owner'
    $project = Join-Path $scratch 'title with spaces'; [void][IO.Directory]::CreateDirectory($project)
    Assert (@(Get-AdditionalCodegenManifests $project $build).Count -eq 0) 'ordinary title requires an additional manifest list'
    $manifest = Join-Path $project 'signin_17559_manifest.toml'
    [IO.File]::WriteAllText($manifest, '[project]')
    $manifestList = Join-Path $build 'x360n-codegen-manifests.txt'
    [IO.File]::WriteAllLines($manifestList, @($manifest.Replace('\','/'), $manifest, ''))
    $manifests = @(Get-AdditionalCodegenManifests $project $build)
    Assert ($manifests.Count -eq 1 -and $manifests[0] -eq $manifest) 'explicit module list mishandled spaces or duplicates'
    foreach ($invalid in @((Join-Path $scratch 'outside_manifest.toml'), (Join-Path $project 'missing_manifest.toml'))) {
        [IO.File]::WriteAllText($manifestList, $invalid)
        $failed = $false
        try { Get-AdditionalCodegenManifests $project $build } catch { $failed = $true }
        Assert $failed 'missing or out-of-project module accepted'
    }
    $generated = Join-Path $build 'embedded.inja.h'; [IO.File]::WriteAllText($generated, 'stale generated template')
    Reset-GeneratedBuildOutputs $build @($generated, $inputFile)
    Assert (-not [IO.File]::Exists($generated)) 'stale generated template survived invalidation'
    Assert ([IO.File]::Exists($inputFile)) 'input outside build was deleted'
    $aotRoot = Join-Path $build 'generated/default'; [void][IO.Directory]::CreateDirectory($aotRoot)
    $aotSource = Join-Path $aotRoot 'sample.cpp'; [IO.File]::WriteAllText($aotSource, 'verified generated source')
    $aotStamp = Join-Path $aotRoot 'codegen.stamp'; [IO.File]::WriteAllText($aotStamp, 'per-module fingerprint')
    [IO.File]::WriteAllText((Join-Path $build '.x360n-aot-owner'), 'full catalog identity')
    [IO.File]::WriteAllText($generated, 'stale embedded template')
    $rootHeader = Join-Path $build 'generated/shared.inja.h'
    [IO.File]::WriteAllText($rootHeader, 'stale non-codegen header')
    Reset-GeneratedBuildOutputs $build @($aotSource,$aotStamp,$generated,$rootHeader)
    Assert ([IO.File]::Exists($aotSource) -and [IO.File]::Exists($aotStamp)) 'compact AOT source/stamp lost during SDK invalidation'
    Assert (-not [IO.File]::Exists($generated)) 'compact AOT incorrectly preserved an unrelated generated template'
    Assert (-not [IO.File]::Exists($rootHeader)) 'AOT ownership incorrectly preserved a non-codegen header at generated root'
    $exe = Join-Path $build 'sample.exe'; [IO.File]::WriteAllText($exe, 'native output')
    $dll = Join-Path $scratch 'runtime.dll'; [IO.File]::WriteAllText($dll, 'built runtime')
    $guest = Join-Path $scratch '.local/inputs/game/HostLoader.dll'
    $native = Join-Path $build 'game_HostLoader.dll'
    $adjacent = Join-Path $scratch '.local/inputs-other/native.DLL'
    $outputs = @(Select-NativeDllOutputs $scratch @($guest,$native,$adjacent,$inputFile))
    Assert ($outputs.Count -eq 2 -and $native -in $outputs -and $adjacent -in $outputs -and $guest -notin $outputs) 'original guest DLL was confused with a native output or boundary sibling was lost'
    $staged = Join-Path $build 'runtime.dll'; [IO.File]::Copy($dll, $staged)
    Assert (@(Get-PreparedArtifacts $build $exe @($dll)).Count -eq 2) 'matching build outputs rejected'
    [IO.File]::WriteAllText($staged, 'wrong version')
    $failed = $false
    try { Get-PreparedArtifacts $build $exe @($dll) | Out-Null } catch { $failed = $true }
    Assert $failed 'publisher accepted a DLL different from the compiled output'
    [IO.File]::Copy($dll, $staged, $true)
    [IO.File]::WriteAllText((Join-Path $build 'obsolete.dll'), 'unproduced binary')
    $failed = $false
    try { Get-PreparedArtifacts $build $exe @($dll) | Out-Null } catch { $failed = $true }
    Assert $failed 'publisher certified a DLL outside the build graph'
    [IO.File]::Delete((Join-Path $build 'obsolete.dll'))
    # A staged Vulkan preflight is bound only when it equals its single build output.
    $preflight = Join-Path $build 'x360n_gpu_preflight.exe'; [IO.File]::WriteAllText($preflight, 'built preflight')
    $failed = $false
    try { Get-PreparedArtifacts $build $exe @($dll) | Out-Null } catch { $failed = $true }
    Assert $failed 'publisher certified a preflight without a build output'
    $preflightOutput = Join-Path $build 'gpu-preflight/Release/x360n_gpu_preflight.exe'
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($preflightOutput)) | Out-Null
    [IO.File]::WriteAllText($preflightOutput, 'other preflight')
    $failed = $false
    try { Get-PreparedArtifacts $build $exe @($dll) | Out-Null } catch { $failed = $true }
    Assert $failed 'publisher accepted a preflight different from the compiled output'
    [IO.File]::Copy($preflight, $preflightOutput, $true)
    $bound = @(Get-PreparedArtifacts $build $exe @($dll))
    Assert ($bound.Count -eq 3 -and @($bound | Where-Object { $_.path -like '*/x360n_gpu_preflight.exe' -or $_.path -like '*\x360n_gpu_preflight.exe' }).Count -eq 1) 'matching preflight was not bound to the receipt'
    [IO.File]::Delete($preflight)
    $manifest = Join-Path $scratch 'profile.toml'; [IO.File]::WriteAllText($manifest, 'fixture')
    $receipt = "$exe.preparation.json"
    Publish-PreparationReceipt $receipt @{schema=1; title='sample'; manifest=$manifest; exe=$exe;
        roles=@{compiler=$inputFile.Replace('\','/'); codegen=$second; build_config=$second};
        inputs=@(Get-ReceiptEntries @($manifest,$inputFile,$second));
        artifacts=@(Get-PreparedArtifacts $build $exe @($dll))}
    Assert ($null -ne (Get-PreviousReceipt $receipt 'sample' $manifest $exe)) 'CMake forward-slash compiler path prevented reuse'
    Write-Output 'PASS: hash substitution, create, replace, publication failure atomicity and path resolution'
} finally {
    $resolved = [IO.Path]::GetFullPath($scratch)
    if ([IO.Path]::GetDirectoryName($resolved) -ne [IO.Path]::GetTempPath().TrimEnd('\')) { throw 'Unexpected scratch path' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
