param([string]$Root = (Resolve-Path "$PSScriptRoot/../..").Path)
$ErrorActionPreference = 'Stop'
# VsDevCmd otherwise starts an asynchronous telemetry process inheriting each
# redirected log. Keep this short fixture synchronous without changing the host.
$env:VSCMD_SKIP_SENDTELEMETRY = '1'
$scratch = Join-Path $Root ('.local/build/aot-module-paths-' + [Guid]::NewGuid().ToString('N'))
$build = Join-Path $scratch 'build'
$module = 'g' + ('a' * 63) + '_MXF_GamerServices_dll'
$source = 'g' + ('a' * 63) + '_register.cpp'
$passed = $false
function Assert($Value, [string]$Message) { if (-not $Value) { throw $Message } }
function Native([string]$Arguments, [string]$Log) {
    cmd /c "`"$Root/scripts/devshell.cmd`" $Arguments > `"$Log`" 2>&1"
    if ($LASTEXITCODE) { throw "Fixture command failed: $Log" }
}
try {
    [void][IO.Directory]::CreateDirectory($scratch)
    $template = [IO.File]::ReadAllText("$Root/titles/common/generic/modules.cmake").Replace('@TITLE@','fixture')
    [IO.File]::WriteAllText("$scratch/modules.cmake", $template)
    [IO.File]::WriteAllText("$scratch/sources.cmake", 'set(GENERATED_SOURCES "${CMAKE_CURRENT_LIST_DIR}/' + $source + '")')
    [IO.File]::WriteAllText("$scratch/value.h", '#define VALUE 1')
    [IO.File]::WriteAllText("$scratch/fixture_pch.h", '#include "value.h"')
    [IO.File]::WriteAllText("$scratch/$source", 'extern "C" __declspec(dllexport) int module_answer(){return VALUE;}')
    $project = @'
cmake_minimum_required(VERSION 3.31)
project(aot_module_paths LANGUAGES CXX)
add_library(runtime INTERFACE)
add_library(rex::runtime ALIAS runtime)
add_custom_target(fixture_codegen)
function(rexglue_apply_recomp_settings name header)
    target_precompile_headers(${name} PRIVATE "${header}")
endfunction()
function(rexglue_configure_module_target name)
endfunction()
include("@ROOT@/cmake/runtime/x360n_ppc.cmake")
cmake_language(DEFER CALL x360n_configure_generated_ppc)
include(modules.cmake)
x360n_add_aot_module(@MODULE@ "${CMAKE_CURRENT_SOURCE_DIR}")
get_property(_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_target IN LISTS _targets)
    get_target_property(_owner ${_target} X360N_AOT_ORIGINAL_TARGET)
    if(_owner STREQUAL "@MODULE@")
        get_target_property(_symbol ${_target} DEFINE_SYMBOL)
        if(NOT _symbol STREQUAL "@MODULE@_EXPORTS")
            message(FATAL_ERROR "Original module export macro changed")
        endif()
        file(WRITE "${CMAKE_BINARY_DIR}/native-target.txt" "${_target}")
    endif()
endforeach()
'@
    $project = $project.Replace('@ROOT@',$Root.Replace('\','/')).Replace('@MODULE@',$module)
    [IO.File]::WriteAllText("$scratch/CMakeLists.txt",$project)
    Native "cmake -S `"$scratch`" -B `"$build`" -G Ninja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Release" "$scratch/configure.log"
    Native "cmake --build `"$build`" --target $module" "$scratch/first.log"
    $dll = Join-Path $build "$module.dll"
    Assert ([IO.File]::Exists($dll)) 'Original DLL filename changed'
    $hash = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash
    Native "cmake --build `"$build`" --target $module" "$scratch/noop.log"
    Assert (-not ([IO.File]::ReadAllText("$scratch/noop.log") -match 'Building|Linking')) 'Unchanged module rebuilt or relinked'
    Assert ((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash -eq $hash) 'No-op altered module bytes'
    Native "ninja -C `"$build`" -t deps" "$scratch/deps.log"
    $deps = [IO.File]::ReadAllText("$scratch/deps.log")
    Assert ($deps -match '\(VALID\)' -and $deps -notmatch '\(STALE\)') 'Native compiler dependencies are stale'
    Assert ($deps.Contains('value.h')) 'PCH header dependency was lost'
    $commands = [IO.File]::ReadAllText("$build/compile_commands.json") | ConvertFrom-Json
    foreach ($command in $commands) {
        Assert ($command.command.Contains('-fno-strict-aliasing') -and $command.command.Contains('-ffp-model=strict')) 'PPC/PCH compile contract changed'
        Assert ($command.command.Contains("${module}_EXPORTS")) 'Original export define changed'
    }
    $native = [IO.File]::ReadAllText("$build/native-target.txt")
    Assert ($native -match '^x360n_aot_[0-9a-f]{20}$') 'Private target is not compact and deterministic'
    Native "cmake -S `"$scratch`" -B `"$build`"" "$scratch/reconfigure.log"
    Assert ([IO.File]::ReadAllText("$build/native-target.txt") -eq $native) 'Target identity changed across configure'
    Native "cmake --build `"$build`" --target $module" "$scratch/reconfigured-noop.log"
    Assert (-not ([IO.File]::ReadAllText("$scratch/reconfigured-noop.log") -match 'Building|Linking')) 'Reconfigure rebuilt unchanged module'
    [IO.File]::WriteAllText("$scratch/value.h", '#define VALUE 2')
    Native "cmake --build `"$build`" --target $module" "$scratch/header.log"
    Assert ([IO.File]::ReadAllText("$scratch/header.log") -match 'Building') 'Changed PCH header was ignored'
    Assert ((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash -ne $hash) 'Changed header did not update module'
    Write-Output 'PASS: compact AOT paths, original DLL/export/build target, deterministic configure, complete deps, PCH/FP contract, no-op and header invalidation'
    $passed = $true
} finally {
    $resolved = [IO.Path]::GetFullPath($scratch)
    $boundary = [IO.Path]::GetFullPath((Join-Path $Root '.local/build')).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($boundary,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected fixture path' }
    if ($passed -and [IO.Directory]::Exists($resolved)) { [IO.Directory]::Delete(('\\?\' + $resolved),$true) }
    elseif (-not $passed) { Write-Warning "Failed fixture preserved: $resolved" }
}
