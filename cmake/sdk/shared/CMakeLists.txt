cmake_minimum_required(VERSION 3.25)
project(x360n_shared_sdk LANGUAGES C CXX)
# glslang's Windows default is relative; keep repeated producer configuration valid.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/install" CACHE PATH "Installation prefix" FORCE)
elseif(NOT IS_ABSOLUTE "${CMAKE_INSTALL_PREFIX}")
    cmake_path(ABSOLUTE_PATH CMAKE_INSTALL_PREFIX BASE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" NORMALIZE)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}" CACHE PATH "Installation prefix" FORCE)
endif()
set(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT FALSE)
include("${X360N_ROOT}/cmake/build/x360n_build_isolation.cmake")
x360n_guard_ninja()
file(TO_CMAKE_PATH "${X360N_ROOT}" X360N_ROOT)
file(TO_CMAKE_PATH "${REXSDK_DIR}" REXSDK_DIR)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT X360N_ROOT OR NOT REXSDK_DIR OR NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "The shared SDK requires an explicit root, pinned SDK and Release")
endif()
option(X360N_PARALLEL_PROBES "Run independent libc checks using original CMake modules in parallel" ON)
if(X360N_PARALLEL_PROBES AND WIN32 AND CMAKE_GENERATOR STREQUAL "Ninja" AND
   CMAKE_C_COMPILER_ID STREQUAL "Clang" AND CMAKE_VERSION VERSION_EQUAL "3.31.6" AND
   NOT CMAKE_TOOLCHAIN_FILE AND NOT CMAKE_TRY_COMPILE_PLATFORM_VARIABLES AND NOT CMAKE_PROJECT_SDL3_INCLUDE)
    set(CMAKE_PROJECT_SDL3_INCLUDE "${X360N_ROOT}/cmake/build/x360n_parallel_probes.cmake")
endif()
add_subdirectory("${REXSDK_DIR}" rexglue-sdk)
include("${X360N_ROOT}/cmake/sdk/x360n_sdk_backports.cmake")
x360n_apply_sdk_backports("${REXSDK_DIR}")
add_custom_target(x360n_shared_sdk DEPENDS rexglue)
_rexglue_get_staged_modules(_shared_modules _shared_module_aliases)
_rexglue_get_runtime_libs(_shared_runtime_libs)
foreach(_module IN LISTS _shared_modules _shared_runtime_libs)
    if(TARGET ${_module})
        add_dependencies(x360n_shared_sdk ${_module})
    endif()
endforeach()

# FidelityFX's own CMake writes its libraries to ${CMAKE_HOME_DIRECTORY}/bin,
# which is this source directory, shared by every generation. Keep them in the
# generation: the DLLs next to rexbase (the codegen producer and title
# staging take native modules from there), import/static libraries apart.
function(x360n_fidelityfx_outputs directory runtime archive)
    get_property(_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _targets)
        get_target_property(_type "${_target}" TYPE)
        if(_type STREQUAL "UTILITY" OR _type STREQUAL "INTERFACE_LIBRARY")
            continue()
        endif()
        foreach(_suffix "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
            set_target_properties("${_target}" PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY${_suffix} "${runtime}"
                LIBRARY_OUTPUT_DIRECTORY${_suffix} "${runtime}"
                ARCHIVE_OUTPUT_DIRECTORY${_suffix} "${archive}")
        endforeach()
    endforeach()
    get_property(_children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(_child IN LISTS _children)
        x360n_fidelityfx_outputs("${_child}" "${runtime}" "${archive}")
    endforeach()
endfunction()
if(REXGLUE_ENABLE_FIDELITYFX AND REXGLUE_FIDELITYFX_SOURCE_DIR AND EXISTS "${REXGLUE_FIDELITYFX_SOURCE_DIR}/ffx-api")
    get_target_property(_x360n_runtime_dir rexbase RUNTIME_OUTPUT_DIRECTORY)
    if(NOT _x360n_runtime_dir)
        message(FATAL_ERROR "rexbase has no runtime output directory for FidelityFX")
    endif()
    x360n_fidelityfx_outputs("${REXGLUE_FIDELITYFX_SOURCE_DIR}/ffx-api"
        "${_x360n_runtime_dir}" "${CMAKE_CURRENT_BINARY_DIR}/fidelityfx/$<CONFIG>")
endif()

function(x360n_sdk_targets directory)
    get_property(_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _targets)
        # These upstream libraries already export build-tree targets separately.
        # Exporting them again makes dependencies in upstream's own exports
        # ambiguous (notably SDL3-static -> SDL3_Headers and SDL3 headers).
        if(_target IN_LIST X360N_UPSTREAM_EXPORTS)
            continue()
        endif()
        get_target_property(_type "${_target}" TYPE)
        if(NOT _type STREQUAL "UTILITY")
            # Keep real names in the export. CMake rewrites internal alias
            # references; consumers recreate only the public SDK aliases below.
            set_property(TARGET "${_target}" PROPERTY EXPORT_NAME "${_target}")
            set_property(GLOBAL APPEND PROPERTY X360N_SDK_EXPORT_TARGETS "${_target}")
        endif()
    endforeach()
    get_property(_children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(_child IN LISTS _children)
        x360n_sdk_targets("${_child}")
    endforeach()
endfunction()
set(X360N_UPSTREAM_EXPORTS SDL3-shared SDL3-static SDL3_test SDL3_Headers)
x360n_sdk_targets("${REXSDK_DIR}")
get_property(_export GLOBAL PROPERTY X360N_SDK_EXPORT_TARGETS)
export(TARGETS ${_export} FILE "${CMAKE_CURRENT_BINARY_DIR}/targets.cmake")
set(_aliases "")
foreach(_pair "sdk|rexsdk" "base|rexbase" "ppc|rexppc" "app|rexapp" "kernel|rexkernel"
        "filesystem|rexfilesystem" "xenos|rexxenos" "gpu|rexgpu" "ui|rexui"
        "apu|rexapu" "hid|rexhid" "xam|rexxam" "xbdm|rexxbdm" "codegen|rexcodegen"
        "gpu-xenos-base|rexgpu-xenos-base" "gpu-xenos-null|rexgpu-xenos-null"
        "gpu-xenos-vulkan|rexgpu-xenos-vulkan" "gpu-xenos-d3d12|rexgpu-xenos-d3d12"
        "apu-sdl3|rexapu-sdl3" "hid-sdl3|rexhid-sdl3" "ui-sdl3|rexui-sdl3" "hid-xinput|rexhid-xinput")
    string(REPLACE "|" ";" _parts "${_pair}")
    list(GET _parts 0 _alias)
    list(GET _parts 1 _target)
    if(TARGET ${_target})
        string(APPEND _aliases "add_library(rex::${_alias} ALIAS ${_target})\n")
    endif()
endforeach()
string(APPEND _aliases "add_executable(rex::rexglue ALIAS rexglue)\n")
# Upstream aliases linked by this project's own SDK consumers (src/native's
# Native plugin). The export keeps real names, so recreate them as the source
# tree defines them; an alias whose target is not exported fails here, in the
# shared SDK build, instead of in a title's configure.
foreach(_alias spdlog::spdlog spdlog::spdlog_header_only xxHash::xxhash Vulkan::Headers glslang::SPIRV)
    if(NOT TARGET "${_alias}")
        continue()
    endif()
    get_target_property(_real "${_alias}" ALIASED_TARGET)
    if(NOT _real)
        continue()  # not an alias in the source tree
    endif()
    if(NOT _real IN_LIST _export)
        message(FATAL_ERROR "Shared SDK alias ${_alias} names a target that is not exported: ${_real}")
    endif()
    string(APPEND _aliases "if(NOT TARGET ${_alias})\nadd_library(${_alias} ALIAS ${_real})\nendif()\n")
endforeach()
set(_checks "")
foreach(_setting CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_C_COMPILER_ARG1 CMAKE_CXX_COMPILER_ARG1
        CMAKE_C_COMPILER_TARGET CMAKE_CXX_COMPILER_TARGET CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN
        CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_C_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELEASE
        CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_RELWITHDEBINFO
        CMAKE_C_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_EXE_LINKER_FLAGS CMAKE_SHARED_LINKER_FLAGS
        CMAKE_MSVC_RUNTIME_LIBRARY CMAKE_POSITION_INDEPENDENT_CODE CMAKE_INTERPROCEDURAL_OPTIMIZATION CMAKE_BUILD_TYPE)
    string(APPEND _checks "if(NOT \"\${${_setting}}\" STREQUAL [==[${${_setting}}]==])\nmessage(FATAL_ERROR \"Shared SDK ${_setting} differs; prepare again with matching flags\")\nendif()\n")
endforeach()
get_cmake_property(_cache_variables CACHE_VARIABLES)
foreach(_setting IN LISTS _cache_variables)
    if(_setting MATCHES "^(REXGLUE_|SDL_|FMT_|SPDLOG_|GLSLANG_|ENABLE_|TRACY_|VULKAN_)")
        get_property(_type CACHE "${_setting}" PROPERTY TYPE)
        if(_type STREQUAL "BOOL" OR _type STREQUAL "STRING")
            if("${${_setting}}" MATCHES "\\]==\\]")
                message(FATAL_ERROR "Unsupported bracket delimiter in shared SDK setting ${_setting}")
            endif()
            string(APPEND _checks "if(DEFINED ${_setting} AND NOT \"\${${_setting}}\" STREQUAL [==[${${_setting}}]==])\nmessage(FATAL_ERROR \"Shared SDK ${_setting} differs; declare the option in the title preset or prepare with X360N_SHARED_SDK=0\")\nendif()\nset(${_setting} [==[${${_setting}}]==])\n")
        endif()
    endif()
endforeach()
set(_packages "find_package(Threads REQUIRED)\n")
if(TARGET Vulkan::Headers)
    string(APPEND _packages "find_package(Vulkan REQUIRED)\n")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sdk.cmake"
    "${_checks}${_packages}include(\"${CMAKE_CURRENT_BINARY_DIR}/rexglue-sdk/thirdparty/sdl3/SDL3headersTargets.cmake\")\ninclude(\"${CMAKE_CURRENT_BINARY_DIR}/rexglue-sdk/thirdparty/sdl3/SDL3sharedTargets.cmake\")\ninclude(\"${CMAKE_CURRENT_BINARY_DIR}/targets.cmake\")\n${_aliases}set(REXGLUE_ROOT \"${REXSDK_DIR}\")\nset(REXGLUE_SHARE_DIR \"${REXSDK_DIR}/src/gpu/xenos\")\nset(REXGLUE_USE_VULKAN ${REXGLUE_USE_VULKAN})\nset(REXGLUE_USE_D3D12 ${REXGLUE_USE_D3D12})\ninclude(\"${REXSDK_DIR}/cmake/rexglue_module.cmake\")\ninclude(\"${REXSDK_DIR}/cmake/rexglue_project.cmake\")\n")
