param([string]$Ninja, [string]$Root = (Resolve-Path "$PSScriptRoot/../..").Path, [string]$BuildScripts)
$ErrorActionPreference = 'Stop'
$VerbosePreference = 'Continue'
$env:PSModulePath = Join-Path $PSHOME 'Modules'
if (-not $BuildScripts) { $BuildScripts = "$Root/scripts/build" }
. "$BuildScripts/preparation-receipt.ps1"
. "$BuildScripts/incremental-state.ps1"
$scratch = Join-Path $Root ('.local/build/incremental-test-' + [Guid]::NewGuid().ToString('N'))
$build = Join-Path $scratch 'build'
[void][IO.Directory]::CreateDirectory($scratch)
function Assert($Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Build-Fixture {
    & "$Root/scripts/devshell.cmd" cmake --build $build *> "$scratch/build.log"
    if ($LASTEXITCODE) { throw ([IO.File]::ReadAllText("$scratch/build.log")) }
}
function Configure-Fixture([string]$Flag='1') {
    & "$Root/scripts/devshell.cmd" cmake -S $scratch -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_CXX_COMPILER=$Root/.local/toolchains/llvm-22.1.8/bin/clang++.exe" "-DVALUE=$Flag" *> "$scratch/configure.log"
    if ($LASTEXITCODE) { throw ([IO.File]::ReadAllText("$scratch/configure.log")) }
}
function Change-Content([string]$File, [string]$Text) {
    $time = [IO.File]::GetLastWriteTimeUtc($File)
    [IO.File]::WriteAllText($File, $Text)
    [IO.File]::SetLastWriteTimeUtc($File, $time)
}
function Snapshot {
    @{schema=1; build=$build; title='fixture'; products=@(Get-IncrementalProducts $build | Where-Object sha256 -ne '<missing>');
        database=@(Get-IncrementalDatabase $build); policy=@(Get-ReceiptEntries @("$scratch/policy"))}
}
function Invalidate($State) {
    Update-IncrementalProducts $build $State ([Collections.Generic.HashSet[string]]::new()) -CheckProducts
}
try {
    $transitionRoot = Join-Path $scratch 'transition'
    $legacyScripts = @{
        'scripts/build-title.ps1'='24dc32959f876804387f934b3b49718db87facd995ec08efe755de6a4e0988dd'
        'scripts/build/incremental-state.ps1'='438e6eecac8bd3199155fd534a57935912bec24c817d83ef37574fe855ee43b9'
        'scripts/build/preparation-receipt.ps1'='03383e4edf99f84a77645ede3a0b3244d770a752a6fa5b7eed861b769d1bf408'
    }
    $oldPolicy = @($legacyScripts.Keys | ForEach-Object { [pscustomobject]@{path=(Join-Path $transitionRoot $_);sha256=$legacyScripts[$_]} })
    $oldPolicy += [pscustomobject]@{path=(Join-Path $transitionRoot 'compiler');sha256=('a'*64)}
    $currentPolicy = @($oldPolicy | ForEach-Object { [pscustomobject]@{path=$_.path;sha256=$(if($_.path -like '*compiler'){$_.sha256}else{'b'*64})} })
    $currentPolicy += [pscustomobject]@{path=(Join-Path $transitionRoot 'scripts/build/product-fingerprint.cs');sha256=('c'*64)}
    Assert (Test-FingerprintPolicyTransition $oldPolicy $currentPolicy $transitionRoot $oldPolicy) 'known authenticated signature-policy transition rejected'
    Assert (-not (Test-FingerprintPolicyTransition $oldPolicy $currentPolicy $transitionRoot @())) 'transition accepted without receipt anchors'
    Assert (-not (Test-FingerprintPolicyTransition $oldPolicy $currentPolicy "$transitionRoot-other" $oldPolicy)) 'transition accepted another source root'
    $compilerPolicy = $currentPolicy | Where-Object path -like '*compiler'
    $compilerPolicy.sha256 = 'd'*64
    Assert (-not (Test-FingerprintPolicyTransition $oldPolicy $currentPolicy $transitionRoot $oldPolicy)) 'transition accepted changed compiler/toolchain policy'
    $compilerPolicy.sha256 = 'a'*64
    $oldPolicy[0].sha256 = 'e'*64
    Assert (-not (Test-FingerprintPolicyTransition $oldPolicy $currentPolicy $transitionRoot $oldPolicy)) 'transition accepted an unknown predecessor'
    $compiledScripts = @{
        'scripts/build-title.ps1'='af68e9112a9df731877ae358cc65e60d3a8b91727df137fd89143e02f300b419'
        'scripts/build/incremental-state.ps1'='a6ed6385306883251cbabfcd8cbfe494ea5e88c960714183652fdbd5e215a09d'
        'scripts/build/preparation-receipt.ps1'='57a3ec59065d92fce079b9b726b24cb82bc7c4f5533a136013d0ad42d39e76bb'
        'scripts/build/product-fingerprint.cs'='91606b61c4e41f59749d01ae8d962224c80f93e71de90f2967a48513377f3107'
    }
    $oldCompiled = @($compiledScripts.Keys | ForEach-Object { [pscustomobject]@{path=(Join-Path $transitionRoot $_);sha256=$compiledScripts[$_]} })
    $currentCompiled = @($oldCompiled | ForEach-Object { [pscustomobject]@{path=$_.path;sha256=('a'*64)} })
    Assert (Test-FingerprintPolicyTransition $oldCompiled $currentCompiled $transitionRoot $oldCompiled) 'known compiled receipt transition rejected'
    $oldCompiled[0].sha256 = 'f'*64
    Assert (-not (Test-FingerprintPolicyTransition $oldCompiled $currentCompiled $transitionRoot $oldCompiled)) 'compiled transition accepted an unknown source'
    $frameworkScripts = @{
        'scripts/build-title.ps1'='a9b8309d4886ff4b3a0ebf17757107c639312ead85f65efb1da5c75d78a490ab'
        'scripts/build/incremental-state.ps1'='0605c5a2e192b7404e4cf22d2388917a2c6a51b3e5894acf9a336736b5179612'
        'scripts/build/preparation-receipt.ps1'='dbad7717a14410298ebf217e05767b531bd731974d65396638ea021b502dcb0d'
        'scripts/build/product-fingerprint.cs'='f3f86e3d3363e87dbb576e1aaa359b695d546d474ca9080a672057a4b9eef361'
    }
    $oldFramework = @($frameworkScripts.Keys | ForEach-Object { [pscustomobject]@{path=(Join-Path $transitionRoot $_);sha256=$frameworkScripts[$_]} })
    $currentFramework = @($oldFramework | ForEach-Object { [pscustomobject]@{path=$_.path;sha256=('b'*64)} })
    Assert (Test-FingerprintPolicyTransition $oldFramework $currentFramework $transitionRoot $oldFramework) 'known Framework policy transition rejected'
    $oldFramework[0].sha256 = 'f'*64
    Assert (-not (Test-FingerprintPolicyTransition $oldFramework $currentFramework $transitionRoot $oldFramework)) 'Framework transition accepted an unknown source'
    $longDirectory = Join-Path $scratch (('long-dependency-' * 7) + '/' + ('nested-header-' * 7))
    $longInput = Join-Path $longDirectory 'input.h'
    $extended = '\\?\' + [IO.Path]::GetFullPath($longDirectory)
    [void][IO.Directory]::CreateDirectory($extended)
    [IO.File]::WriteAllText(($extended + '\input.h'), 'long path input')
    Assert ($longInput.Length -gt 260) 'long-path fixture is too short'
    $longEntries = @(Get-ReceiptEntries @($longInput))
    Assert (Test-ReceiptEntries $longEntries) 'existing long dependency treated as absent'
    [IO.File]::WriteAllText(($extended + '\input.h'), 'long path edit')
    Assert (-not (Test-ReceiptEntries $longEntries)) 'long dependency content edit accepted'
    # Use relative Ninja outputs, as CMake does for long AOT object names. The
    # older VS Ninja cannot stat absolute output aliases above MAX_PATH.
    $longGraph = Join-Path $scratch 'long-graph'
    [void][IO.Directory]::CreateDirectory($longGraph)
    $longNode = ('long-directory-' * 7) + '/' + ('nested-product-' * 7) + '/native.cache'
    $longProduct = Resolve-BuildPath $longGraph $longNode
    [void][IO.Directory]::CreateDirectory((Get-BuildIoPath (Split-Path $longProduct)))
    [IO.File]::WriteAllText((Get-BuildIoPath $longProduct), 'long native product')
    [IO.File]::WriteAllText("$longGraph/input.h", 'long product input')
    [IO.File]::WriteAllText("$longGraph/CMakeCache.txt", "CMAKE_MAKE_PROGRAM:FILEPATH=$Ninja`n")
    [IO.File]::WriteAllText("$longGraph/build.ninja", "rule custom`n  command = unused-generator`nbuild $longNode`: custom input.h`nbuild all: phony $longNode`nbuild rexglue: phony`n")
    $longProducts = @(Get-IncrementalProducts $longGraph)
    Assert ($longProducts.Count -eq 1 -and $longProducts[0].sha256 -ne '<missing>') 'long product omitted from checkpoint'
    [IO.File]::WriteAllText((Get-BuildIoPath $longProduct), 'corrupted long native product')
    Update-IncrementalProducts $longGraph @{products=$longProducts} ([Collections.Generic.HashSet[string]]::new()) -CheckProducts
    Assert (-not (Test-BuildFile $longProduct)) 'long native product corruption was reused'
    $responseGraph = [IO.File]::ReadAllText("$longGraph/build.ninja").Replace('command = unused-generator', 'command = unused-generator @$rspfile' + "`n  rspfile = flags.rsp`n  rspfile_content = -DFLAG=1")
    [IO.File]::WriteAllText("$longGraph/build.ninja", $responseGraph)
    $responseProducts = @(Get-IncrementalProducts $longGraph)
    [IO.File]::WriteAllText("$longGraph/build.ninja", $responseGraph.Replace('-DFLAG=1','-DFLAG=2'))
    Assert (@(Get-IncrementalProducts $longGraph)[0].signature -ne $responseProducts[0].signature) 'response file flag change ignored'
    foreach ($argument in @('@external.rsp', '@"external flags.rsp"', '"@external flags.rsp"', '-Wl,@external.rsp', '-Xclang=@external.rsp', 'cmd.exe /C "cd . && compiler @external.rsp && cd ."')) {
        $rejected = $false
        try { Assert-IncrementalResponseFlags ("compiler $argument").Replace('compiler cmd.exe', 'cmd.exe') }
        catch { $rejected = $_.Exception.Message -like 'Unexpanded response file:*' }
        Assert $rejected "untracked response flags accepted: $argument"
    }
    Assert-IncrementalResponseFlags 'compiler -DMAIL="user@example.com" "C:/folder @part/source.cpp" -I"@include"'
    [IO.File]::WriteAllText("$longGraph/build.ninja", $responseGraph.Replace('unused-generator @$rspfile', 'unused-generator @external.rsp'))
    $rejected = $false
    try { $null = @(Get-IncrementalProducts $longGraph) } catch { $rejected = $_.Exception.Message -like 'Unexpanded response file:*' }
    Assert $rejected 'native checkpoint certified an unexpanded response argument'
    [IO.File]::WriteAllText("$scratch/CMakeLists.txt", @'
cmake_minimum_required(VERSION 3.25)
project(incremental_fixture LANGUAGES CXX)
add_custom_target(rexglue)
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/generated/generated.h" COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/generated" COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_SOURCE_DIR}/template.h" "${CMAKE_BINARY_DIR}/generated/generated.h" DEPENDS template.h)
add_executable(fixture "first source.cpp" second.cpp main.cpp "${CMAKE_BINARY_DIR}/generated/generated.h")
target_include_directories(fixture PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(fixture PRIVATE VALUE=${VALUE})
target_link_options(fixture PRIVATE "LINKER:/MANIFESTINPUT:${CMAKE_SOURCE_DIR}/application.manifest")
set_property(TARGET fixture APPEND PROPERTY LINK_DEPENDS "${CMAKE_SOURCE_DIR}/application.manifest")
'@)
    [IO.File]::WriteAllText("$scratch/application.manifest", '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0"></assembly>')
    [IO.File]::WriteAllText("$scratch/shared.h", '#define SHARED 1')
    [IO.File]::WriteAllText("$scratch/template.h", '#define GENERATED 1')
    [IO.File]::WriteAllText("$scratch/first source.cpp", "#include `"shared.h`"`n#include `"generated.h`"`nint first() { return SHARED + GENERATED + VALUE; }")
    [IO.File]::WriteAllText("$scratch/second.cpp", "#include `"shared.h`"`nint second() { return SHARED; }")
    [IO.File]::WriteAllText("$scratch/main.cpp", 'int first(); int second(); int main() { return first() + second(); }')
    [IO.File]::WriteAllText("$scratch/policy", 'SDK/compiler/patch identity')
    Configure-Fixture; Build-Fixture
    [IO.File]::WriteAllText("$scratch/CMakePresets.json", '{"version":6,"include":["flags.json"]}')
    [IO.File]::WriteAllText("$scratch/flags.json", '{"version":6}')
    [IO.File]::WriteAllText("$build/generated/dll_targets.cmake", '# auxiliary list not included by the project')
    $receiptInputs = @(Get-BuildInputs $Root 'fixture' $build @('fixture'))
    Assert (([IO.Path]::GetFullPath("$build/generated/dll_targets.cmake")) -in $receiptInputs) 'auxiliary generated CMake list missing from receipt'
    $configureInputs = @(Get-IncrementalConfigureInputs $build $scratch)
    Assert (Test-IncrementalConfigureInputs $configureInputs $configureInputs) 'identical configuration was rejected'
    [IO.File]::Delete("$build/generated/dll_targets.cmake")
    Assert (-not (Test-IncrementalConfigureInputs @(Get-IncrementalConfigureInputs $build $scratch) $configureInputs)) 'removed auxiliary CMake list ignored'
    [IO.File]::WriteAllText("$build/generated/dll_targets.cmake", '# auxiliary list not included by the project')
    Change-Content "$scratch/flags.json" '{"version":6,"configurePresets":[]}'
    Assert (-not (Test-IncrementalConfigureInputs @(Get-IncrementalConfigureInputs $build $scratch) $configureInputs)) 'inherited preset change ignored'
    Change-Content "$scratch/flags.json" '{"version":6}'
    Change-Content "$scratch/second.cpp" "#include `"shared.h`"`nint second() { return SHARED; } // configure independent"
    Assert (Test-IncrementalConfigureInputs @(Get-IncrementalConfigureInputs $build $scratch) $configureInputs) 'source-only change requires configure'
    $cmakeText = [IO.File]::ReadAllText("$scratch/CMakeLists.txt")
    Change-Content "$scratch/CMakeLists.txt" ($cmakeText + "`n# same timestamp configuration change`n")
    Assert (-not (Test-IncrementalConfigureInputs @(Get-IncrementalConfigureInputs $build $scratch) $configureInputs)) 'same-mtime CMake change ignored'
    Change-Content "$scratch/CMakeLists.txt" $cmakeText
    [IO.File]::WriteAllText("$build/generated/new-sources.cmake", '# newly generated module list')
    Assert (-not (Test-IncrementalConfigureInputs @(Get-IncrementalConfigureInputs $build $scratch) $configureInputs)) 'new generated module list ignored'
    [IO.File]::Delete("$build/generated/new-sources.cmake")
    Build-Fixture
    $state = Snapshot
    $objects = @($state.products | Where-Object path -match '[.]obj$')
    Assert ($objects.Count -eq 3) 'fixture did not record every native object'
    $first = @($objects | Where-Object path -match 'first_source')[0].path
    if (-not $first) { $first = @($objects | Where-Object path -match 'first')[0].path }
    $second = @($objects | Where-Object path -match 'second')[0].path
    $main = @($objects | Where-Object path -match 'main')[0].path
    Invalidate $state
    Assert ([IO.File]::Exists($first) -and [IO.File]::Exists($second) -and [IO.File]::Exists($main)) 'no-change invalidated an object'
    Change-Content "$scratch/second.cpp" "#include `"shared.h`"`nint second() { return SHARED + 1; }"
    Invalidate $state
    Assert (-not [IO.File]::Exists($second) -and [IO.File]::Exists($first) -and [IO.File]::Exists($main)) 'same-mtime source edit did not invalidate exactly its object'
    Build-Fixture; $state = Snapshot
    Change-Content "$scratch/shared.h" '#define SHARED 2'
    Invalidate $state
    Assert (-not [IO.File]::Exists($first) -and -not [IO.File]::Exists($second) -and [IO.File]::Exists($main)) 'shared header did not invalidate exactly both consumers'
    Build-Fixture; $state = Snapshot
    [IO.File]::Move("$scratch/shared.h", "$scratch/replaced.h")
    Change-Content "$scratch/first source.cpp" "#include `"replaced.h`"`n#include `"generated.h`"`nint first() { return SHARED + GENERATED + VALUE; }"
    Change-Content "$scratch/second.cpp" "#include `"replaced.h`"`nint second() { return SHARED; }"
    Invalidate $state
    Assert (-not [IO.File]::Exists($first) -and -not [IO.File]::Exists($second) -and [IO.File]::Exists($main)) 'removed old compiler dependency was incorrectly reused'
    Build-Fixture; $state = Snapshot
    Change-Content "$scratch/template.h" '#define GENERATED 2'
    $processed = [Collections.Generic.HashSet[string]]::new()
    Update-IncrementalProducts $build $state $processed -CheckProducts
    Assert (-not [IO.File]::Exists("$build/generated/generated.h")) 'same-mtime template retained its generated header'
    # Tool-build phase produces the header before final invalidation, as in the driver.
    & "$Root/scripts/devshell.cmd" cmake --build $build --target generated/generated.h *> "$scratch/generate.log"
    Assert ($LASTEXITCODE -eq 0) 'header generator failed'
    Update-IncrementalProducts $build $state $processed
    Assert (-not [IO.File]::Exists($first) -and [IO.File]::Exists($second)) 'new generated content was not invalidated after generation'
    Build-Fixture; $state = Snapshot
    Change-Content $main 'corrupt object with original timestamp'
    Invalidate $state
    Assert (-not [IO.File]::Exists($main) -and [IO.File]::Exists($first)) 'tampered intermediate reused'
    Build-Fixture; $state = Snapshot
    Change-Content "$scratch/application.manifest" '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><assembly xmlns="urn:schemas-microsoft-com:asm.v1" manifestVersion="1.0"><!-- changed --></assembly>'
    Invalidate $state
    Assert (-not [IO.File]::Exists("$build/fixture.exe") -and [IO.File]::Exists($first) -and [IO.File]::Exists($main)) 'same-mtime link manifest did not invalidate only the link'
    Build-Fixture; $state = Snapshot
    Configure-Fixture '2'; Invalidate $state
    Assert (@($objects | Where-Object { [IO.File]::Exists($_.path) }).Count -eq 0) 'flag change did not invalidate all consumers'
    Build-Fixture; $state = Snapshot
    $implicit = Join-Path $build '.preparation/toolchain.json'
    [void][IO.Directory]::CreateDirectory((Split-Path $implicit))
    $library = Join-Path $scratch 'implicit.lib'
    [IO.File]::WriteAllText($library, 'implicit library v1')
    Publish-PreparationReceipt $implicit @{libraries=@($library); resources=@()}
    $state = Snapshot
    Change-Content $library 'implicit library v2'
    Invalidate $state
    Assert (-not [IO.File]::Exists("$build/fixture.exe") -and [IO.File]::Exists($first) -and [IO.File]::Exists($main)) 'implicit library change did not invalidate only the link'
    Build-Fixture; $state = Snapshot
    $file = "$build/incremental.json"
    Publish-PreparationReceipt $file $state
    $previous = @{incremental=(Get-ReceiptEntries @($file))}
    Assert ($null -ne (Read-IncrementalState $file $previous $build 'fixture' $false)) 'valid checkpoint rejected'
    Assert ($null -eq (Read-IncrementalState $file $previous $build 'other' $false)) 'cross-title checkpoint accepted'
    Assert ($null -eq (Read-IncrementalState $file $previous $build 'fixture' $true)) 'interrupted build reused checkpoint'
    $logBytes = [IO.File]::ReadAllBytes("$build/.ninja_log")
    [IO.File]::AppendAllText("$build/.ninja_log", "`n")
    Assert ($null -eq (Read-IncrementalState $file $previous $build 'fixture' $false)) 'modified Ninja database accepted'
    [IO.File]::WriteAllBytes("$build/.ninja_log", $logBytes)
    Change-Content "$scratch/policy" 'changed SDK/compiler/patch identity'
    Assert ($null -eq (Read-IncrementalState $file $previous $build 'fixture' $false)) 'changed SDK/compiler/patch policy accepted'
    [IO.File]::WriteAllText($file, '{truncated')
    Assert ($null -eq (Read-IncrementalState $file $previous $build 'fixture' $false)) 'truncated checkpoint accepted'
    Write-Output 'PASS: no-op, source, shared/generated headers, flags, implicit libraries, same-mtime corruption, interruption, isolation and invalid cache'
} finally {
    $resolved = [IO.Path]::GetFullPath($scratch)
    $boundary = [IO.Path]::GetFullPath((Join-Path $Root '.local/build')).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($boundary, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected scratch path' }
    [IO.Directory]::Delete(('\\?\' + $resolved), $true)
}
