param([string]$Root = (Resolve-Path "$PSScriptRoot/../..").Path)
$ErrorActionPreference = 'Stop'
$env:PSModulePath = Join-Path $PSHOME 'Modules'
. "$Root/scripts/preparation-receipt.ps1"
$scratch = Join-Path ([IO.Path]::GetTempPath()) ("x360n-receipt-" + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($scratch)
function Assert($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
try {
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
    $exe = Join-Path $build 'sample.exe'; [IO.File]::WriteAllText($exe, 'native output')
    $dll = Join-Path $scratch 'runtime.dll'; [IO.File]::WriteAllText($dll, 'built runtime')
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
