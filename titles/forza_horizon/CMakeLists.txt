# forza_horizon - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(forza_horizon LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(verify-input.cmake)

include(generated/rexglue.cmake)

# x360-native: fixes for the pinned SDK commit, applied from outside the SDK
# tree (see cmake/x360n_sdk_backports.cmake). Only meaningful with REXSDK_DIR.
if(REXSDK_DIR)
    include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/x360n_sdk_backports.cmake)
    x360n_apply_sdk_backports("${REXSDK_DIR}")
endif()

# Sources
set(FORZAHORIZON_SOURCES
    src/main.cpp
    ../common/launch_handoff.cpp
)
include_directories(../common)

if(WIN32)
    add_executable(forza_horizon WIN32 ${FORZAHORIZON_SOURCES})
else()
    add_executable(forza_horizon ${FORZAHORIZON_SOURCES})
endif()

# GPU emulation is a runtime-loaded plugin (cvar gpu_plugin, empty = disabled);
# stage it next to the executable so --gpu_plugin=xenos can load it.
rexglue_setup_target(forza_horizon GPU_PLUGINS xenos)
add_custom_target(forza_horizon_identity
    COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/verify-input.cmake")
add_dependencies(forza_horizon_codegen forza_horizon_identity)

# x360-native: copy rexruntime and the GPU plugin next to the executable on every
# build, not only when it relinks (see cmake/x360n_sdk_backports.cmake).
if(REXSDK_DIR)
    x360n_stage_runtime_dlls(forza_horizon PLUGIN_TARGETS rexgpu-xenos)
endif()
