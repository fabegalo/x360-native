# dash_17559 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(dash_17559 LANGUAGES CXX)

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
set(DASH17559_SOURCES
    src/main.cpp
    # Traps for imports the pinned SDK does not provide at link time (see the file).
    src/unresolved_imports.cpp
    ../common/camera_state.cpp
    src/typeface.cpp
    src/launch_uri.cpp
    src/boot_intro.cpp
)

if(WIN32)
    add_executable(dash_17559 WIN32 ${DASH17559_SOURCES})
else()
    add_executable(dash_17559 ${DASH17559_SOURCES})
endif()

# GPU emulation is a runtime-loaded plugin (cvar gpu_plugin, empty = disabled);
# stage it next to the executable so --gpu_plugin=xenos can load it.
rexglue_setup_target(dash_17559 GPU_PLUGINS xenos)

# x360-native: copy rexruntime and the GPU plugin next to the executable on every
# build, not only when it relinks (see cmake/x360n_sdk_backports.cmake).
if(REXSDK_DIR)
    x360n_stage_runtime_dlls(dash_17559 PLUGIN_TARGETS rexgpu-xenos)
endif()
