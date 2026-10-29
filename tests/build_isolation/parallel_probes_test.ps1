param([string]$Root=(Resolve-Path "$PSScriptRoot/../..").Path)
$ErrorActionPreference='Stop'
$scratch=Join-Path $Root ('.local/build/parallel-probes-test-'+[Guid]::NewGuid().ToString('N'))
$source=Join-Path $scratch 'source with space/sdk/thirdparty/sdl3'
[void][IO.Directory]::CreateDirectory((Join-Path $source 'includes'))
[IO.File]::WriteAllText((Join-Path $source 'includes/custom.h'), @'
#if !defined(REQUIRED_FLAG) || !defined(REQUIRED_DEFINITION)
#error missing caller check context
#endif
#define custom_feature 1
extern int declared_but_unresolved(void);
'@)
$hook=(Join-Path $Root 'cmake/build/x360n_parallel_probes.cmake').Replace('\','/')
$cmake=@'
cmake_minimum_required(VERSION 3.16)
project(probe_equivalence LANGUAGES C)
set(REXSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
get_filename_component(REXSDK_DIR "${REXSDK_DIR}" ABSOLUTE)
if(TEST_PARALLEL)
  include("@HOOK@")
endif()
include(CheckIncludeFile)
include(CheckSymbolExists)
set(CMAKE_REQUIRED_FLAGS "-DREQUIRED_FLAG=1")
set(CMAKE_REQUIRED_DEFINITIONS -DREQUIRED_DEFINITION=1)
set(CMAKE_REQUIRED_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/includes")
set(headers_to_check stdio.h stdlib.h stdint.h custom.h missing_x360n_header.h)
set(available_headers "")
foreach(header IN LISTS headers_to_check)
  string(TOUPPER "${header}" identifier)
  string(REPLACE "." "_" identifier "${identifier}")
  check_include_file("${header}" "LIBC_HAS_${identifier}")
  if(LIBC_HAS_${identifier})
    list(APPEND available_headers "${header}")
  endif()
endforeach()
set(symbols_to_check fopen malloc EOF SEEK_SET custom_feature declared_but_unresolved missing_x360n_symbol)
foreach(symbol IN LISTS symbols_to_check)
  string(TOUPPER "${symbol}" identifier)
  check_symbol_exists("${symbol}" "${available_headers}" "LIBC_HAS_${identifier}")
endforeach()
get_cmake_property(variables CACHE_VARIABLES)
list(SORT variables)
set(result "")
foreach(variable IN LISTS variables)
  if(variable MATCHES "^LIBC_HAS_")
    string(APPEND result "${variable}=${${variable}}\n")
  endif()
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/features.txt" "${result}")
'@
[IO.File]::WriteAllText((Join-Path $source 'CMakeLists.txt'),$cmake.Replace('@HOOK@',$hook))
foreach($mode in @('OFF','ON')) {
    $build=Join-Path $scratch $mode
    & "$Root/scripts/devshell.cmd" cmake -S $source -B $build -G Ninja "-DTEST_PARALLEL=$mode" "-DCMAKE_C_COMPILER=$Root/.local/toolchains/llvm-22.1.8/bin/clang.exe" -DCMAKE_C_FLAGS=-march=x86-64-v2 -DCMAKE_BUILD_TYPE=Release *> "$scratch/$mode.log"
    if($LASTEXITCODE){throw ([IO.File]::ReadAllText("$scratch/$mode.log"))}
}
$serial=[IO.File]::ReadAllText((Join-Path $scratch 'OFF/features.txt'))
$parallel=[IO.File]::ReadAllText((Join-Path $scratch 'ON/features.txt'))
if($serial -cne $parallel){throw "Serial/parallel CMake feature results differ: $scratch"}
if($parallel -notmatch 'LIBC_HAS_CUSTOM_FEATURE=1' -or $parallel -notmatch 'LIBC_HAS_DECLARED_BUT_UNRESOLVED=\r?\n' -or
    $parallel -notmatch 'LIBC_HAS_MISSING_X360N_HEADER_H=\r?\n'){throw 'Probe oracle did not cover required flags, missing headers and link failure'}
if(-not (Get-ChildItem (Join-Path $scratch 'ON/CMakeFiles') -Directory -Filter 'x360n-probes-*').Count){throw 'Parallel probes were not invoked'}
Write-Output "PASS: original serial/parallel CMake oracles match for headers, macros, flags, absent symbols and unresolved link; $scratch"
