# castle_crashers - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(castle_crashers LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# x360-native: fixes for the pinned SDK commit, applied from outside the SDK
# tree (see cmake/x360n_sdk_backports.cmake). Only meaningful with REXSDK_DIR.
if(REXSDK_DIR)
    include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/x360n_sdk_backports.cmake)
    x360n_apply_sdk_backports("${REXSDK_DIR}")
endif()

# Sources
set(CASTLECRASHERS_SOURCES
    src/main.cpp
    ../common/launch_handoff.cpp
)

if(WIN32)
    add_executable(castle_crashers WIN32 ${CASTLECRASHERS_SOURCES})
else()
    add_executable(castle_crashers ${CASTLECRASHERS_SOURCES})
endif()

# GPU emulation is a runtime-loaded plugin (cvar gpu_plugin, empty = disabled);
# stage it next to the executable so --gpu_plugin=xenos can load it.
rexglue_setup_target(castle_crashers GPU_PLUGINS xenos)

# x360-native: copy rexruntime and the GPU plugin next to the executable on every
# build, not only when it relinks (see cmake/x360n_sdk_backports.cmake).
if(REXSDK_DIR)
    x360n_stage_runtime_dlls(castle_crashers PLUGIN_TARGETS rexgpu-xenos)
endif()
