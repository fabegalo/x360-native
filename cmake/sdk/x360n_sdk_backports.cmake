# Integration with the pinned ReXGlue SDK, shared by the top-level project and by
# title projects under titles/ that consume the SDK through REXSDK_DIR:
#  - backports of upstream fixes, applied from outside the SDK tree (CMake only);
#  - verification of the local source patches in patches/rexglue-sdk/ (applied
#    to the checkout by scripts/setup/apply-sdk-patches.ps1), at configure and build time;
#  - x360n_stage_runtime_dlls(), which keeps the DLLs next to an executable current.
#
# Each backport is tied to the exact pinned commit it is needed for and cites
# the upstream commit that fixes it; moving the pin disables it automatically.

include(${CMAKE_CURRENT_LIST_DIR}/x360n_verify_sdk_patches.cmake)
set(X360N_LOCK_FILE "${CMAKE_CURRENT_LIST_DIR}/../../deps.lock.json")
get_filename_component(X360N_LOCK_FILE "${X360N_LOCK_FILE}" ABSOLUTE)
set(X360N_VERIFY_PATCHES_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/x360n_verify_sdk_patches.cmake")

# x360n_sdk_commit(<out_var> <sdk_dir>): HEAD of the SDK checkout, or "" on error.
function(x360n_sdk_commit out_var sdk_dir)
    execute_process(
        COMMAND git -C "${sdk_dir}" rev-parse HEAD
        OUTPUT_VARIABLE _head OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _rc ERROR_QUIET)
    if(NOT _rc EQUAL 0)
        set(_head "")
    endif()
    set(${out_var} "${_head}" PARENT_SCOPE)
endfunction()

# Call after the SDK has been added with add_subdirectory().
function(x360n_apply_sdk_backports sdk_dir)
    include(${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../runtime/x360n_ppc.cmake)
    cmake_language(DEFER CALL x360n_configure_generated_ppc)
    if(TARGET ppc_tests)
        target_link_libraries(ppc_tests PRIVATE x360n_ppc_options)
    endif()
    # One staging producer per destination, including hosts configured by the SDK.
    set(REXGLUE_EXTERNAL_STAGING ON PARENT_SCOPE)
    include(${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../build/x360n_build_isolation.cmake)
    x360n_guard_ninja()

    get_target_property(_imported rexruntime IMPORTED)
    if(_imported)
        # The driver authenticated an immutable generic generation, including
        # its compiler, flags, dependencies and every original SDK source.
        # Imported libraries already carry the producer's usage requirements.
        x360n_verify_sdk_patches("${sdk_dir}" "${X360N_LOCK_FILE}")
        return()
    endif()

    if(WIN32 AND TARGET rexglue)
        # The preparation tool consumes the same UTF-8 manifest/file paths as
        # title processes. Do not change the machine's ANSI code page.
        target_link_options(rexglue PRIVATE
            "LINKER:/MANIFESTINPUT:${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../runtime/x360n_runtime.manifest")
        set_property(TARGET rexglue APPEND PROPERTY LINK_DEPENDS
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../runtime/x360n_runtime.manifest")
    endif()
    x360n_sdk_commit(_commit "${sdk_dir}")
    if(_commit STREQUAL "f5337cdc947ff6d4c4196737e2c807a48f2a1fc2")  # v0.10.0
        # upstream b5e0cf8 "expose the imgui and xxHash headers public SDK headers
        # include": include/rex/hash.h includes <xxhash.h> and include/rex/ui/style.h
        # includes <imgui.h>, but both libraries are linked PRIVATE. Without this,
        # unit_tests (hash_test.cpp) and every consumer compiling rex_app.cpp fail.
        target_include_directories(rexruntime PUBLIC
            $<BUILD_INTERFACE:${sdk_dir}/thirdparty/imgui>
            $<BUILD_INTERFACE:${sdk_dir}/thirdparty/xxHash>)
        message(STATUS "x360n: applied SDK backport b5e0cf8 (imgui/xxHash include dirs) for ${_commit}")
    endif()

    # Local source patches (deps.lock.json "rexglue_sdk.patches") are for the
    # locked commit only. X360N_SDK_PATCHES=OFF exists only to reproduce the
    # state before the patches (their tests must then fail); it is never the
    # supported configuration.
    option(X360N_SDK_PATCHES "Require the local SDK patches in patches/rexglue-sdk" ON)
    file(READ "${X360N_LOCK_FILE}" _lock)
    string(JSON _locked GET "${_lock}" rexglue_sdk commit)
    if(NOT X360N_SDK_PATCHES)
        message(WARNING "x360n: X360N_SDK_PATCHES=OFF: the SDK is built as checked out, "
                        "local patches not verified. Only for reproducing the unpatched state.")
    elseif(_commit STREQUAL _locked)
        x360n_verify_sdk_patches("${sdk_dir}" "${X360N_LOCK_FILE}")
        message(STATUS "x360n: local SDK patches verified (patches/rexglue-sdk) for ${_commit}")
        # Re-check on every build, before rexruntime compiles, so a patch reverted
        # after configuring cannot be built silently.
        if(NOT TARGET x360n_verify_sdk_patches)
            add_custom_target(x360n_verify_sdk_patches
                COMMAND ${CMAKE_COMMAND} -DSDK_DIR=${sdk_dir} -DLOCK_FILE=${X360N_LOCK_FILE}
                        -P ${X360N_VERIFY_PATCHES_SCRIPT}
                COMMENT "x360n: verifying local SDK patches"
                VERBATIM)
            add_dependencies(rexruntime x360n_verify_sdk_patches)
        endif()
    else()
        message(WARNING "x360n: SDK at ${_commit}, local patches are for ${_locked}; not applied")
    endif()
endfunction()

# x360n_stage_runtime_dlls(<target> [PLUGIN_TARGETS <t>...])
# The SDK copies an executable's runtime DLLs (rexruntime, ...) and GPU plugins
# next to it only as a POST_BUILD step of that executable
# (cmake/rexglue_helpers.cmake), which does not run when only a DLL changed: the
# executable does not relink, and the copy next to it stays old. This adds a
# target that copies them when they differ, built before the executable itself:
# building only that executable (--target) stages them too.
function(x360n_stage_runtime_dlls target)
    # The SDK accepts UTF-8 cvars and uses narrow filesystem paths internally.
    # Windows 10 1903+ can declare UTF-8 per process without changing the user's
    # system locale. This covers every title using the common runtime staging.
    # https://learn.microsoft.com/windows/apps/design/globalizing/use-utf8-code-page
    get_target_property(_kind ${target} TYPE)
    if(WIN32 AND _kind STREQUAL "EXECUTABLE")
        target_link_options(${target} PRIVATE
            "LINKER:/MANIFESTINPUT:${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../runtime/x360n_runtime.manifest")
        set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../runtime/x360n_runtime.manifest")
    endif()
    if(APPLE)
        set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "@loader_path")
    elseif(UNIX)
        set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "$ORIGIN")
    endif()
    cmake_parse_arguments(ARG "" "" "PLUGIN_TARGETS" ${ARGN})
    if(REXGLUE_USE_VULKAN AND "rexgpu-xenos" IN_LIST ARG_PLUGIN_TARGETS)
        if(NOT TARGET x360n_gpu_preflight)
            add_executable(x360n_gpu_preflight "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../src/gpu_preflight.cpp")
            target_link_libraries(x360n_gpu_preflight PRIVATE rexruntime)
            set_target_properties(x360n_gpu_preflight PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/gpu-preflight/$<CONFIG>")
            if(APPLE)
                set_property(TARGET x360n_gpu_preflight APPEND PROPERTY BUILD_RPATH "@loader_path")
            elseif(UNIX)
                set_property(TARGET x360n_gpu_preflight APPEND PROPERTY BUILD_RPATH "$ORIGIN")
            endif()
        endif()
        list(APPEND ARG_PLUGIN_TARGETS x360n_gpu_preflight)
    endif()
    # The launcher can select either renderer without preparing a title again.
    # Stage both supported plugins; selection and experimental access stay in
    # the session settings. Other platforms / D3D12-disabled builds omit Native.
    set(_native_default OFF)
    if(WIN32 AND REXGLUE_USE_D3D12)
        set(_native_default ON)
    endif()
    option(X360N_NATIVE_RENDERER "Build and stage the experimental Native graphics plugin" ${_native_default})
    if(X360N_NATIVE_RENDERER AND "rexgpu-xenos" IN_LIST ARG_PLUGIN_TARGETS)
        if(NOT WIN32)
            message(FATAL_ERROR "The experimental Native renderer is currently implemented only on Windows")
        endif()
        if(NOT TARGET rexgpu-native)
            if(NOT REXGLUE_USE_D3D12)
                message(FATAL_ERROR "The Windows Native plugin includes D3D12 and requires REXGLUE_USE_D3D12; Vulkan is built alongside it when enabled")
            endif()
            set(X360_REXGLUE_DIR "${REXSDK_DIR}")
            set(X360N_NATIVE_PLUGIN_ONLY ON)
            add_subdirectory("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../../src/native" "${CMAKE_BINARY_DIR}/x360n-native")
        endif()
        list(APPEND ARG_PLUGIN_TARGETS rexgpu-native)
        list(REMOVE_DUPLICATES ARG_PLUGIN_TARGETS)
    endif()
    # All callers in this directory share one producer. Each configuration gets
    # a generated staging script; no executable post-link copy competes with it.
    string(SHA256 _key "${CMAKE_CURRENT_BINARY_DIR}")
    set(_stage "x360n_stage_${_key}")
    if(NOT TARGET ${_stage})
        add_custom_target(${_stage}
            COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_BINARY_DIR}/x360n-stage-$<CONFIG>.cmake"
            COMMENT "x360n: staging runtime DLLs in ${CMAKE_CURRENT_BINARY_DIR}"
            VERBATIM)
        cmake_language(DEFER CALL x360n_finish_staging)
    endif()
    set_property(DIRECTORY APPEND PROPERTY X360N_STAGE_TARGETS ${target})
    foreach(_p IN LISTS ARG_PLUGIN_TARGETS)
        set_property(DIRECTORY APPEND PROPERTY X360N_STAGE_EXTRA "$<TARGET_FILE:${_p}>")
    endforeach()
    foreach(_fx amd_fidelityfx_vk amd_fidelityfx_dx12)
        if(TARGET ${_fx})
            set_property(DIRECTORY APPEND PROPERTY X360N_STAGE_EXTRA "$<TARGET_FILE:${_fx}>")
            add_dependencies(${_stage} ${_fx})
        endif()
    endforeach()
    add_dependencies(${_stage} rexruntime ${ARG_PLUGIN_TARGETS})
    # Imported targets have no producer edge in a consumer's Ninja graph. Keep
    # their DLL files explicit so staging and receipt inventories see the same
    # authenticated inputs as CMake's runtime-DLL generator expressions.
    set(_native_files "$<TARGET_RUNTIME_DLLS:${target}>")
    if(NOT WIN32)
        # ELF/Mach-O shared libraries are not listed by TARGET_RUNTIME_DLLS.
        set(_native_files "")
        set_property(DIRECTORY APPEND PROPERTY X360N_STAGE_EXTRA "$<TARGET_FILE:rexruntime>")
    endif()
    foreach(_native rexruntime ${ARG_PLUGIN_TARGETS} amd_fidelityfx_vk amd_fidelityfx_dx12)
        if(TARGET ${_native})
            list(APPEND _native_files "$<TARGET_FILE:${_native}>")
        endif()
    endforeach()
    string(SHA256 _input_key "${target};${_native_files}")
    set(_input_target "x360n_runtime_inputs_${_input_key}")
    if(NOT TARGET ${_input_target})
        add_custom_target(${_input_target} DEPENDS ${_native_files})
    endif()
    add_dependencies(${_stage} ${_input_target})
    add_dependencies(${target} ${_stage})
endfunction()

function(x360n_finish_staging)
    get_property(_targets DIRECTORY PROPERTY X360N_STAGE_TARGETS)
    get_property(_extra DIRECTORY PROPERTY X360N_STAGE_EXTRA)
    set(_content "")
    foreach(_target IN LISTS _targets)
        string(APPEND _content "set(sources \"$<TARGET_RUNTIME_DLLS:${_target}>;${_extra}\")\nset(destination \"$<TARGET_FILE_DIR:${_target}>\")\ninclude(\"${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../build/x360n_stage_files.cmake\")\n")
    endforeach()
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/x360n-stage-$<CONFIG>.cmake" CONTENT "${_content}")
endfunction()
