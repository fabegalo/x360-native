# dash_6717 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(dash_6717 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)
if(REXSDK_DIR)
    include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/x360n_sdk_backports.cmake)
    x360n_apply_sdk_backports("${REXSDK_DIR}")
endif()

# Sources
set(DASH6717_SOURCES
    src/main.cpp
    src/unresolved_imports.cpp
    src/curl_bind.cpp
    ../common/camera_state.cpp
)

if(WIN32)
    add_executable(dash_6717 WIN32 ${DASH6717_SOURCES})
else()
    add_executable(dash_6717 ${DASH6717_SOURCES})
endif()

rexglue_setup_target(dash_6717 GPU_PLUGINS xenos)
if(REXSDK_DIR)
    x360n_stage_runtime_dlls(dash_6717 PLUGIN_TARGETS rexgpu-xenos)
endif()
