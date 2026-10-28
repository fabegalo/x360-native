param([Parameter(Mandatory)][string]$ToolWorkspace)
$ErrorActionPreference='Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$scratch = Join-Path $root ('.local/tmp/staging-check-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force "$scratch/tests/desktop" | Out-Null
[IO.File]::WriteAllText("$scratch/tests/desktop/targets.cmake",'# no tests in configure fixture')
[IO.File]::WriteAllText("$scratch/stub.cpp",'int main() { return 0; }')
$cmake = @'
cmake_minimum_required(VERSION 3.25)
project(maintenance_staging LANGUAGES CXX)
add_library(x360n_core INTERFACE)
add_library(tomlplusplus INTERFACE)
add_executable(x360n stub.cpp)
add_executable(x360n-extract stub.cpp)
set(X360_REXGLUE_DIR "__TOOLS__/external/rexglue-sdk")
set(X360_PACKAGE_PAYLOAD_SHA256 "0000000000000000000000000000000000000000000000000000000000000000")
add_subdirectory("__SOURCE__/src/desktop" desktop)
if(X360_DISTRIBUTION)
  if(TARGET x360n_stage_desktop_tools)
    message(FATAL_ERROR "Private distribution still stages unused adjacent tools")
  endif()
else()
  if(NOT TARGET x360n_stage_desktop_tools)
    message(FATAL_ERROR "Development launcher lost adjacent tools")
  endif()
endif()
get_target_property(deps x360-native MANUALLY_ADDED_DEPENDENCIES)
foreach(tool x360n x360n-extract)
  if(NOT tool IN_LIST deps)
    message(FATAL_ERROR "Launcher lost tool dependency: ${tool}")
  endif()
endforeach()
'@
$cmake=$cmake.Replace('__TOOLS__',$ToolWorkspace.Replace('\','/')).Replace('__SOURCE__',$root.Replace('\','/'))
[IO.File]::WriteAllText("$scratch/CMakeLists.txt",$cmake)
$llvm = Join-Path $ToolWorkspace '.local/toolchains/clang-22.1.8/bin/clang++.exe'
# Use the pinned compiler's declared install directory, never PATH clang.
$lock=Get-Content -Raw (Join-Path $ToolWorkspace 'deps.lock.json') | ConvertFrom-Json
$llvm=Join-Path $ToolWorkspace ($lock.llvm.install_dir+'/bin/clang++.exe')
foreach($mode in @('OFF','ON')) {
    & (Join-Path $ToolWorkspace 'scripts/devshell.cmd') cmake -S $scratch -B "$scratch/build-$mode" -G Ninja "-DCMAKE_CXX_COMPILER=$llvm" "-DX360_DISTRIBUTION=$mode" -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE) { throw "Staging configure failed: $mode" }
}
# No launcher, game or compiler target is executed by this configure-only check.
$absolute=[IO.Path]::GetFullPath($scratch)
if (-not $absolute.StartsWith((Join-Path $root '.local/tmp/staging-check-'),[StringComparison]::OrdinalIgnoreCase)) { throw 'Fixture root mismatch' }
Remove-Item -LiteralPath $absolute -Recurse -Force
'PASS: actual desktop CMake preserves development staging, avoids private distribution copies, and retains both tool dependencies'
