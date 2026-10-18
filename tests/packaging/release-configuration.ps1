$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. "$root/scripts/build/release-configuration.ps1"
$scratch = Join-Path $root ('.local/evidence/release-configuration-tests-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($scratch)
function Assert($condition, $message) { if (-not $condition) { throw $message } }
Assert (-not (Test-ReleaseBuildConfiguration $scratch)) 'Missing configuration accepted'
foreach ($mode in @('Debug','RelWithDebInfo','','Release')) {
    [IO.File]::WriteAllText("$scratch/CMakeCache.txt", "CMAKE_BUILD_TYPE:STRING=$mode`r`n")
    Assert ((Test-ReleaseBuildConfiguration $scratch) -eq ($mode -eq 'Release')) "Wrong configuration accepted: $mode"
}
foreach ($bad in @("CMAKE_BUILD_TYPE:STRING=release`n", "CMAKE_BUILD_TYPE:STRING=Release`nCMAKE_BUILD_TYPE:STRING=Debug`n")) {
    [IO.File]::WriteAllText("$scratch/CMakeCache.txt", $bad)
    Assert (-not (Test-ReleaseBuildConfiguration $scratch)) 'Ambiguous/noncanonical cache accepted'
}
Assert-ReleaseCompileCommands @(@{file='src/host.cpp';command='clang++ -O3 -DNDEBUG --dependent-lib=libcmt -c host.cpp'},
    @{file='resources/app.rc';command='rc /nologo app.rc'})
Assert-ReleaseCompileCommands @(@{file='generated/title.cpp';output='cmake_object_order_depends_target_title';command=''},
    @{file='generated/title.cpp';output='title.cpp.obj';command='clang++ -O3 -DNDEBUG -c title.cpp'}) -NinjaGraph
$rejected=$false
try { Assert-ReleaseCompileCommands @(@{file='generated/title.cpp';output='title.cpp.obj';command='clang++ -O0 -c title.cpp'}) -NinjaGraph }
catch { $rejected=$true }
Assert $rejected 'Debug object accepted in full Ninja graph'
$json='[{"file":"host.cpp","output":"host.cpp.obj","command":"clang++ -O3 -DNDEBUG -c host.cpp"}]'
Assert-ReleaseCompileCommands @($json | ConvertFrom-Json) -NinjaGraph
$rejected=$false
try { Assert-ReleaseCompileCommands @($json.Replace('-O3','-O0') | ConvertFrom-Json) -NinjaGraph }
catch { $rejected=$true }
Assert $rejected 'Decoded nested JSON accepted a Debug object'
$negative = @('', '-O0 -DNDEBUG', '-O3', '-O3 -DNDEBUG -D_DEBUG', '-O3 -DNDEBUG --dependent-lib=libcmtd',
    '-O3 -DNDEBUG /MDd', '-O3 -DNDEBUG -O2', '-O3 -DNDEBUG -D_ITERATOR_DEBUG_LEVEL=2',
    '-O3 -DNDEBUG -UNDEBUG', '-O3 -DNDEBUG --dependent-lib=libcpmtd', '-O3 -DNDEBUG -Og')
foreach ($flags in $negative) {
    $rejected = $false
    try { Assert-ReleaseCompileCommands @(@{file='src/host.cpp';command="clang++ $flags -c host.cpp"}) }
    catch { Assert ($_.Exception.Message -like 'Non-Release compiler*') 'Unexpected rejection'; $rejected=$true }
    Assert $rejected "Bad flags accepted: $flags"
}
$rejected=$false
try { Assert-ReleaseCompileCommands @() } catch { $rejected=$true }
Assert $rejected 'Empty evidence accepted'
$rootPresets = Get-Content "$root/CMakePresets.json" -Raw | ConvertFrom-Json
Assert ($rootPresets.configurePresets[0].cacheVariables.CMAKE_DEFAULT_BUILD_TYPE -eq 'Release') 'Root default is not Release'
Assert (($rootPresets.buildPresets | Where-Object name -eq 'win-amd64-release').configuration -eq 'Release') 'Root Release build preset changed'
foreach ($file in Get-ChildItem "$root/titles" -Recurse -Filter CMakePresets.json) {
    $presets=Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
    $standard=@($presets.configurePresets | Where-Object name -eq 'x360n-win-amd64')
    Assert ($standard.Count -eq 1 -and $standard[0].cacheVariables.CMAKE_BUILD_TYPE -eq 'Release') "Non-Release standard preset: $file"
    foreach($preset in $presets.configurePresets | Where-Object {$_.name -like '*-relwithdebinfo'}) {
        Assert ($preset.cacheVariables.CMAKE_BUILD_TYPE -eq 'RelWithDebInfo') 'Explicit diagnostic preset changed'
    }
}
foreach ($file in Get-ChildItem "$root/titles" -Recurse -Filter preparation.toml) {
    $runtime = @(Get-Content -LiteralPath $file.FullName | Where-Object {$_ -match '^runtime_files\s*='}) -join ' '
    Assert ($runtime -notmatch '(?:rexruntime(?:d|rd)|rexgpu-xenos(?:d|rd)|TracyClient(?:d|rd))\.dll') "Non-Release runtime metadata: $file"
    if($runtime) { Assert ($runtime.Contains('"rexruntime.dll"') -and $runtime.Contains('"rexgpu-xenos.dll"')) "Missing Release runtime metadata: $file" }
}
"PASS: cache modes, compiler evidence, $($negative.Count) adverse flag cases, empty evidence, six standard presets, explicit diagnostic presets and Release runtime metadata"
