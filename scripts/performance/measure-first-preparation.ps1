# Reproducible, bounded official driver run. Does not reset/delete any data.
# Reset only task-owned derived state separately; state is recorded, not inferred
# from a warm timing. Downloads/import/registration are measured separately.
param([Parameter(Mandatory)][string]$Title,[Parameter(Mandatory)][string]$Output,
    [ValidateSet('FirstBuild','NewTitle','Repeat','Validation')][string]$Scenario='Validation',
    [switch]$DisableSharedSdk,[ValidateRange(1,64)][int]$Jobs=16)
$ErrorActionPreference='Stop'
$root=(Resolve-Path "$PSScriptRoot/../..").Path
if($Scenario -ne 'Validation' -and -not @([Diagnostics.Process]::GetProcessesByName('System') | Where-Object Id -eq 4).Count){
    throw 'Benchmark requires host process visibility; a restricted process namespace cannot exclude competing builds/sessions'
}
. "$root/scripts/build/preparation-receipt.ps1"
$project=Join-Path $root "titles/$Title"
$build=Join-Path $root ".local/build/titles/$Title"
if(-not(Test-Path (Join-Path $project 'CMakePresets.json'))){
    $project=Join-Path $root ".local/aot/$Title"
    $preset=Get-Content (Join-Path $project 'CMakePresets.json') -Raw | ConvertFrom-Json
    $build=($preset.configurePresets | Where-Object name -eq 'x360n-win-amd64').binaryDir
}
$outputPath=[IO.Path]::GetFullPath($Output)
if(Test-Path -LiteralPath $outputPath){throw 'Evidence output already exists'}
[void][IO.Directory]::CreateDirectory($outputPath)
$stamps=@()
foreach($directory in @((Join-Path $project 'generated'),(Join-Path $build 'generated'))){
    if(Test-Path -LiteralPath $directory){$stamps+=@(Get-ChildItem -LiteralPath $directory -Recurse -File -Filter codegen.stamp | ForEach-Object FullName)}
}
$cache=Join-Path $root '.local/cache/shared-sdk'
$state=[ordered]@{scenario=$Scenario;title=$Title;shared_sdk=(-not $DisableSharedSdk);jobs=$Jobs;
    build=$build;cache_file_exists=(Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt'));
    receipt_exists=(Test-Path -LiteralPath (Join-Path $build "$Title.exe.preparation.json"));
    codegen_stamps=$stamps;shared_sdk_pointers=@(Get-ChildItem $cache -File -Filter '*.json' -ErrorAction SilentlyContinue | ForEach-Object Name);
    deps_lock_sha256=(Get-FileHash "$root/deps.lock.json").Hash.ToLowerInvariant();
    sdk_commit=(Get-Content "$root/deps.lock.json" -Raw | ConvertFrom-Json).rexglue_sdk.commit;
    windows_page_cache='not flushed or controlled';patch_count=([IO.File]::ReadAllText("$root/deps.lock.json") | ConvertFrom-Json).rexglue_sdk.patches.Count;
    downloads='not performed by this runner';scope='official preparation driver; excludes UI, import, SDK download/install and registration'}
if($Scenario -in @('FirstBuild','NewTitle') -and ($state.cache_file_exists -or $state.receipt_exists -or $stamps.Count)){throw 'Cold title state required; preserve/reset task-owned build and generated outputs before measuring'}
if($Scenario -eq 'FirstBuild' -and -not $DisableSharedSdk -and $state.shared_sdk_pointers.Count){throw 'FirstBuild requires an empty shared SDK index'}
if($Scenario -eq 'Repeat' -and -not $state.receipt_exists){throw 'Repeat requires a completed preparation receipt'}
$state | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputPath 'conditions.json')
Add-Type -Path "$root/scripts/build/preparation-process.cs"
$previousMetrics=$env:X360N_PREPARATION_METRICS
$previousShared=$env:X360N_SHARED_SDK
$previousJobs=$env:CMAKE_BUILD_PARALLEL_LEVEL
try {
    $env:X360N_PREPARATION_METRICS=Join-Path $outputPath 'phases.jsonl'
    $env:X360N_SHARED_SDK=if($DisableSharedSdk){'0'}else{'1'}
    $env:CMAKE_BUILD_PARALLEL_LEVEL=[string]$Jobs
    $powershell=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
    $log=Join-Path $outputPath 'preparation.log'
    $command='"'+$powershell+'" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$root+'/scripts/build-title.ps1" -Title '+$Title
    $arguments='/d /s /c "'+$command+' > "'+$log+'" 2>&1"'
    $result=[X360NPreparationProcess]::Run($env:ComSpec,$arguments,$root,2400,$Scenario -ne 'Validation')
    $result | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $outputPath 'result.json')
    if($result.exit_code){Get-Content -LiteralPath $log -Tail 20;throw "Preparation benchmark failed: $($result.exit_code)"}
    $objects=0
    if(Test-Path (Join-Path $outputPath 'phases.jsonl')){
        foreach($row in (Get-Content (Join-Path $outputPath 'phases.jsonl') | ForEach-Object {$_ | ConvertFrom-Json})){
            if(Test-Path -LiteralPath $row.log){$objects+=@([IO.File]::ReadAllLines($row.log) | Where-Object {$_ -match '^\[\d+/\d+\] Building (C|CXX) object '}).Count}
        }
    }
    @{native_object_compile_actions=$objects;includes_configure_probes=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $outputPath 'objects.json')
    Write-Output ("PASS: {0} {1}; {2:N3} s; {3} native objects; evidence {4}" -f $Scenario,$Title,$result.wall_seconds,$objects,$outputPath)
}finally{
    $env:X360N_PREPARATION_METRICS=$previousMetrics
    $env:X360N_SHARED_SDK=$previousShared
    $env:CMAKE_BUILD_PARALLEL_LEVEL=$previousJobs
}
