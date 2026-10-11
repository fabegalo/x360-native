# Backports of upstream ReXGlue fixes, applied from outside the SDK tree so
# external/rexglue-sdk stays unmodified. Shared by the top-level project and by
# title projects under titles/ that consume the SDK through REXSDK_DIR.
#
# Each backport is tied to the exact pinned commit it is needed for and cites
# the upstream commit that fixes it; moving the pin disables it automatically.

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
endfunction()
