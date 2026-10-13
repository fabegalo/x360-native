# motocross_madness - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(motocross_madness LANGUAGES CXX)

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
set(MOTOCROSSMADNESS_SOURCES
    src/main.cpp
    src/unresolved_imports.cpp
    src/avatar_trace.cpp
    src/xam_aot_probe.cpp
    ../common/avatar/avatar_toc.cpp
    ../common/avatar/avatar_metadata.cpp
    ../common/avatar/avatar_assets_request.cpp
    ../common/avatar/avatar_aot_assets.cpp
    ../common/camera_state.cpp
    ../common/launch_handoff.cpp
)

if(WIN32)
    add_executable(motocross_madness WIN32 ${MOTOCROSSMADNESS_SOURCES})
else()
    add_executable(motocross_madness ${MOTOCROSSMADNESS_SOURCES})
endif()

# GPU emulation is a runtime-loaded plugin (cvar gpu_plugin, empty = disabled);
# stage it next to the executable so --gpu_plugin=xenos can load it.
rexglue_setup_target(motocross_madness GPU_PLUGINS xenos)

# Original XAM 2.0.17559.0 ([[modules]] x360nsys:\xam.xex). Its guest dispatch table
# would start at 0x81C00000 and cover 0x8242D510, over the game image at 0x82000000;
# patch 0031 keeps it on the host.
if(TARGET motocross_madness_xam)
    target_compile_definitions(motocross_madness_xam PRIVATE REX_HOST_FUNCTION_TABLE=1)
    # Read-only diagnostic hooks of analysis/xam17559_avatar_trace.toml.
    target_sources(motocross_madness_xam PRIVATE src/xam_avatar_trace.cpp)
endif()

# x360-native: copy rexruntime and the GPU plugin next to the executable on every
# build, not only when it relinks (see cmake/x360n_sdk_backports.cmake).
if(REXSDK_DIR)
    x360n_stage_runtime_dlls(motocross_madness PLUGIN_TARGETS rexgpu-xenos)
endif()

# Optional focused import checks; no title boot and no previously completed suites.
option(X360N_LDI_CHECKS "Build synthetic LDI import checks" OFF)
if(X360N_LDI_CHECKS)
    enable_testing()
    add_executable(x360n_ldi_checks ../../tests/sdkcheck/ldi_test.cpp)
    target_link_libraries(x360n_ldi_checks PRIVATE rexruntime)
    x360n_stage_runtime_dlls(x360n_ldi_checks)
    add_test(NAME sdkcheck.LDI COMMAND x360n_ldi_checks)
endif()

# Standalone lifecycle checks; independent of LDI and the existing avatar suites.
option(X360N_AVATAR_ASSETS_CHECKS "Build avatar request lifecycle checks" OFF)
if(X360N_AVATAR_ASSETS_CHECKS)
    enable_testing()
    add_executable(x360n_avatar_assets_checks
        ../../tests/avatar/assets_request_test.cpp
        ../common/avatar/avatar_assets_request.cpp)
    add_test(NAME avatar.AssetsRequest COMMAND x360n_avatar_assets_checks)
endif()