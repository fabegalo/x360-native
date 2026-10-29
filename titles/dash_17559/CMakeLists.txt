# dash_17559 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(dash_17559 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(../../cmake/sdk/x360n_title_sdk.cmake)
x360n_include_rexglue(generated/rexglue.cmake)

# x360-native: fixes for the pinned SDK commit, applied from outside the SDK
# tree (see cmake/sdk/x360n_sdk_backports.cmake). Only meaningful with REXSDK_DIR.
if(REXSDK_DIR)
    include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/sdk/x360n_sdk_backports.cmake)
    x360n_apply_sdk_backports("${REXSDK_DIR}")
endif()

# Sources
set(DASH17559_SOURCES
    src/main.cpp
    ../common/dashboard_assets.cpp
    ../common/guest_fault_context.cpp
    ../../src/supervisor/sha256.cpp
    ../../src/supervisor/digest_batch.cpp
    # Traps for imports the pinned SDK does not provide at link time (see the file).
    src/unresolved_imports.cpp
    ../common/camera_state.cpp
    ../common/launch_handoff.cpp
    src/typeface.cpp
    src/launch_uri.cpp
    src/boot_intro.cpp
    src/system_ui.cpp
    src/original_xam_services.cpp
    src/system_ui_images.cpp
    src/display_transition.cpp
)

if(WIN32)
    add_executable(dash_17559 WIN32 ${DASH17559_SOURCES})
    target_link_libraries(dash_17559 PRIVATE bcrypt)
else()
    add_executable(dash_17559 ${DASH17559_SOURCES})
endif()

# GPU emulation is a runtime-loaded plugin (cvar gpu_plugin, empty = disabled);
# stage it next to the executable so --gpu_plugin=xenos can load it.
rexglue_setup_target(dash_17559 GPU_PLUGINS xenos)

add_library(dash_17559_session_ui SHARED src/session_ui_host.cpp src/system_ui.cpp
    ../common/dashboard_assets.cpp ../../src/supervisor/sha256.cpp
    ../../src/supervisor/digest_batch.cpp)
target_compile_definitions(dash_17559_session_ui PRIVATE X360N_SYSTEM_UI_PLUGIN=1)
target_link_libraries(dash_17559_session_ui PRIVATE rex::runtime)
if(WIN32)
    target_link_libraries(dash_17559_session_ui PRIVATE bcrypt)
endif()
add_dependencies(dash_17559 dash_17559_session_ui)

# x360-native: copy rexruntime and the GPU plugin next to the executable on every
# build, not only when it relinks (see cmake/sdk/x360n_sdk_backports.cmake).
if(REXSDK_DIR)
    x360n_stage_runtime_dlls(dash_17559 PLUGIN_TARGETS rexgpu-xenos)
endif()

# Original system UI also queries the camera state; use the shared platform contract.
if(TARGET dash_17559_gamerprofile)
    target_sources(dash_17559_gamerprofile PRIVATE ../common/camera_state.cpp src/system_ui_imports.cpp src/system_ui_images.cpp)
endif()

# Overlapping guest images require separate codegen invocations. A fresh install
# configures the producer first, then generates every module before reconfigure.
option(X360N_CODEGEN_BOOTSTRAP "Configure the producer before guest generation" OFF)
set(X360N_CODEGEN_MANIFESTS "")
foreach(module signin createprofile vk hud deviceselector)
    set(manifest "${CMAKE_CURRENT_SOURCE_DIR}/${module}/${module}_17559_manifest.toml")
    string(APPEND X360N_CODEGEN_MANIFESTS "${manifest}\n")
    set(generated_dir "${CMAKE_CURRENT_SOURCE_DIR}/generated/${module}")
    if(NOT EXISTS "${generated_dir}/sources.cmake")
        if(NOT X360N_CODEGEN_BOOTSTRAP)
            message(FATAL_ERROR "Missing generated ${module}; run scripts/build-title.ps1 -Title dash_17559 to prepare all original modules")
        endif()
        continue()
    endif()
    include("${generated_dir}/sources.cmake")
    add_custom_command(
        OUTPUT "${generated_dir}/codegen.build.stamp" ${GENERATED_SOURCES}
        COMMAND $<TARGET_FILE:rex::rexglue> codegen "${manifest}"
        DEPFILE "${generated_dir}/codegen.d"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM)
    set(module_target "dash_17559_${module}")
    add_library(${module_target} SHARED ${GENERATED_SOURCES} src/system_ui_imports.cpp src/system_ui_images.cpp)
    if(module STREQUAL "createprofile")
        target_sources(${module_target} PRIVATE src/createprofile_imports.cpp)
    endif()
    if(module STREQUAL "signin" OR module STREQUAL "vk")
        target_sources(${module_target} PRIVATE "src/${module}_imports.cpp")
    endif()
    target_include_directories(${module_target} PRIVATE "${generated_dir}")
    # Separate codegen invocations do not populate the SDK's entry/DLL source
    # inventories. These targets still compile PPC and need its shared contract.
    target_link_libraries(${module_target} PRIVATE rex::runtime x360n_ppc_options)
    rexglue_apply_recomp_settings(${module_target} "${generated_dir}/dash_17559_pch.h")
    rexglue_configure_module_target(${module_target} HOST dash_17559)
    add_dependencies(dash_17559 ${module_target})
endforeach()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/x360n-codegen-manifests.txt" "${X360N_CODEGEN_MANIFESTS}")

if(TARGET dash_17559_xam_ui)
    target_sources(dash_17559_xam_ui PRIVATE src/system_ui_memory.cpp)
    # The guest 0x80000000 and 0x90000000 heaps alias: this module's
    # auxiliary table would overwrite the resident dashboard image.
    target_compile_definitions(dash_17559_xam_ui PRIVATE REX_HOST_FUNCTION_TABLE=1)
endif()

include(../../cmake/runtime/x360n_dashboard_assets.cmake)
x360n_guard_dashboard_profile(dash_17559 "${CMAKE_CURRENT_SOURCE_DIR}/dash_17559_manifest.toml")
