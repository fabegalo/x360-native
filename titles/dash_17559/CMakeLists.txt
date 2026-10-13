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
    ../common/launch_handoff.cpp
    src/typeface.cpp
    src/launch_uri.cpp
    src/boot_intro.cpp
    src/system_ui.cpp
    src/display_transition.cpp
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

# Original system UI also queries the camera state; use the shared platform contract.
if(TARGET dash_17559_gamerprofile)
    target_sources(dash_17559_gamerprofile PRIVATE ../common/camera_state.cpp src/system_ui_imports.cpp)
endif()

# Separate AOT image: the SDK generator intentionally rejects overlapping modules
# in one manifest. Runtime session ownership still uses the one existing host.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/signin/sources.cmake")
    include(generated/signin/sources.cmake)
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/generated/signin/codegen.build.stamp"
               ${GENERATED_SOURCES}
        COMMAND $<TARGET_FILE:rex::rexglue> codegen
                "${CMAKE_CURRENT_SOURCE_DIR}/signin/signin_17559_manifest.toml"
        DEPFILE "${CMAKE_CURRENT_SOURCE_DIR}/generated/signin/codegen.d"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM)
    add_library(dash_17559_signin SHARED ${GENERATED_SOURCES}
        src/system_ui_imports.cpp src/signin_imports.cpp)
    target_include_directories(dash_17559_signin PRIVATE generated/signin)
    target_link_libraries(dash_17559_signin PRIVATE rex::runtime)
    rexglue_apply_recomp_settings(dash_17559_signin
        "${CMAKE_CURRENT_SOURCE_DIR}/generated/signin/dash_17559_pch.h")
    rexglue_configure_module_target(dash_17559_signin HOST dash_17559)
    add_dependencies(dash_17559 dash_17559_signin)
else()
    message(FATAL_ERROR "Generate signin/signin_17559_manifest.toml with rexglue codegen before configuring this development target")
endif()

if(TARGET dash_17559_xam_ui)
    target_sources(dash_17559_xam_ui PRIVATE src/system_ui_memory.cpp)
    # The guest 0x80000000 and 0x90000000 heaps alias: this module's
    # auxiliary table would overwrite the resident dashboard image.
    target_compile_definitions(dash_17559_xam_ui PRIVATE REX_HOST_FUNCTION_TABLE=1)
endif()
