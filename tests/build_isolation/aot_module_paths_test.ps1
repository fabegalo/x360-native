param([string]$Root = (Resolve-Path "$PSScriptRoot/../..").Path)
$ErrorActionPreference = 'Stop'
$env:PSModulePath = Join-Path $PSHOME 'Modules'
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
    [IO.File]::WriteAllText("$scratch/modules.cmake", "set(X360N_CUSTOM_MODULES TRUE)`n" + $template)
    [IO.File]::WriteAllText("$scratch/sources.cmake", 'set(GENERATED_SOURCES "${CMAKE_CURRENT_LIST_DIR}/' + $source + '")' + "`nset(GENERATED_PREFIX fixture)`n")
    [IO.File]::WriteAllText("$scratch/value.h", '#define VALUE 1')
    [IO.File]::WriteAllText("$scratch/fixture_pch.h", '#include "value.h"')
    [IO.File]::WriteAllText("$scratch/$source", 'extern "C" __declspec(dllexport) int module_answer(){return VALUE;}')
    $project = @'
cmake_minimum_required(VERSION 3.31)
project(aot_module_paths LANGUAGES CXX)
add_library(sdk INTERFACE)
add_library(rex::sdk ALIAS sdk)
include("@ROOT@/external/rexglue-sdk/cmake/rexglue_project.cmake")
function(rexglue_link_host_allocator name)
endfunction()
function(rexglue_add_codegen name)
    cmake_parse_arguments(ARG "" "MANIFEST;OUTPUT_DIR;WORKING_DIRECTORY" "SOURCES;TOOL" ${ARGN})
    if(NOT ARG_OUTPUT_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR OR NOT ARG_SOURCES)
        message(FATAL_ERROR "Codegen lost its absolute output or generated source inventory")
    endif()
    if(NOT TARGET ${name})
        add_custom_target(${name})
    endif()
endfunction()
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/dll_targets.cmake" "message(FATAL_ERROR \"Custom module inventory ignored\")")
rexglue_project(fixture OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}" MODULE_TARGETS modules.cmake)
if(NOT X360N_CUSTOM_MODULES OR NOT REXGLUE_ENTRYPOINT_GENERATED_SOURCES OR
   NOT REXGLUE_ENTRYPOINT_INCLUDE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "Absolute AOT output or custom module inventory was lost")
endif()
rexglue_project(fixture OUTPUT_DIR . MODULE_TARGETS "${CMAKE_CURRENT_SOURCE_DIR}/modules.cmake")
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/dll_targets.cmake" "set(X360N_DEFAULT_MODULES TRUE)")
rexglue_project(fixture OUTPUT_DIR .)
if(NOT X360N_DEFAULT_MODULES)
    message(FATAL_ERROR "Default SDK module inventory was lost")
endif()
# A stale generated inventory must not block the producer needed to replace it.
include("@ROOT@/cmake/sdk/x360n_title_sdk.cmake")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/fixture-sdk.cmake" "# Existing SDK interfaces above\n")
set(X360N_SHARED_SDK_EXPORT "${CMAKE_CURRENT_BINARY_DIR}/fixture-sdk.cmake")
set(X360N_CODEGEN_BOOTSTRAP ON)
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/dll_targets.cmake" "message(FATAL_ERROR \"Stale SDK module inventory consumed before codegen\")")
x360n_setup_rexglue(fixture OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(X360N_CUSTOM_MODULES FALSE)
x360n_setup_rexglue(fixture OUTPUT_DIR . MODULE_TARGETS modules.cmake)
if(NOT X360N_CUSTOM_MODULES)
    message(FATAL_ERROR "Bootstrap lost the explicit module inventory")
endif()
set(X360N_CODEGEN_BOOTSTRAP OFF)
set(X360N_DEFAULT_MODULES FALSE)
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/dll_targets.cmake" "set(X360N_DEFAULT_MODULES TRUE)")
x360n_setup_rexglue(fixture OUTPUT_DIR .)
if(NOT X360N_DEFAULT_MODULES)
    message(FATAL_ERROR "Normal configure omitted the regenerated module inventory")
endif()
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/host.cpp" "#include <cstdlib>\nextern \"C\" int module_answer();\nint main(int argc,char** argv){return argc != 2 || module_answer() != std::atoi(argv[1]);}\n")
add_executable(fixture_host host.cpp)
rexglue_setup_target(fixture_host NO_COPY)
if(NOT TARGET fixture_recompiled)
    message(FATAL_ERROR "Host lost its generated object library")
endif()
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
    Native "cmake --build `"$build`" --target $module fixture_host" "$scratch/first.log"
    Native "`"$build/fixture_host.exe`" 1" "$scratch/host.log"
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
    $native = [IO.File]::ReadAllText("$build/native-target.txt")
    foreach ($command in $commands) {
        if($command.file.EndsWith($source) -or $command.file.Contains('cmake_pch')) {
            Assert ($command.command.Contains('-fno-strict-aliasing') -and $command.command.Contains('-ffp-model=strict')) 'PPC/PCH compile contract changed'
        }
        if($command.command.Contains($native+'.dir')) {
            Assert ($command.command.Contains("${module}_EXPORTS")) 'Original export define changed'
        }
    }
    Assert ($native -match '^x360n_aot_[0-9a-f]{20}$') 'Private target is not compact and deterministic'
    Native "cmake -S `"$scratch`" -B `"$build`"" "$scratch/reconfigure.log"
    Assert ([IO.File]::ReadAllText("$build/native-target.txt") -eq $native) 'Target identity changed across configure'
    Native "cmake --build `"$build`" --target $module" "$scratch/reconfigured-noop.log"
    Assert (-not ([IO.File]::ReadAllText("$scratch/reconfigured-noop.log") -match 'Building|Linking')) 'Reconfigure rebuilt unchanged module'
    [IO.File]::WriteAllText("$scratch/value.h", '#define VALUE 2')
    Native "cmake --build `"$build`" --target $module" "$scratch/header.log"
    Assert ([IO.File]::ReadAllText("$scratch/header.log") -match 'Building') 'Changed PCH header was ignored'
    Assert ((Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash -ne $hash) 'Changed header did not update module'
    Native "cmake --build `"$build`" --target fixture_host" "$scratch/host-header.log"
    Native "`"$build/fixture_host.exe`" 2" "$scratch/host-header-run.log"
    Write-Output 'PASS: stale-inventory producer bootstrap, explicit/final modules, official project absolute/relative paths and custom/default modules, linked generated host code, compact AOT paths, original DLL/export/build target, deterministic configure, complete deps, PCH/FP contract, no-op and header invalidation'
    $passed = $true
} finally {
    $resolved = [IO.Path]::GetFullPath($scratch)
    $boundary = [IO.Path]::GetFullPath((Join-Path $Root '.local/build')).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($boundary,[StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected fixture path' }
    if ($passed -and [IO.Directory]::Exists($resolved)) { [IO.Directory]::Delete(('\\?\' + $resolved),$true) }
    elseif (-not $passed) { Write-Warning "Failed fixture preserved: $resolved" }
}
