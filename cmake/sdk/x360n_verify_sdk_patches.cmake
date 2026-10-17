# Verifies that the local SDK patches listed in deps.lock.json
# ("rexglue_sdk.patches") are applied to the SDK checkout: every patched file
# must have the SHA-256 the lock records for it after all patches.
#
# Used two ways:
#   include() + x360n_verify_sdk_patches(<sdk_dir> <lock_file>)   at configure time
#   cmake -DSDK_DIR=... -DLOCK_FILE=... -P x360n_verify_sdk_patches.cmake   at build time
# On a mismatch it stops with FATAL_ERROR and says how to fix it.

function(x360n_verify_sdk_patches sdk_dir lock_file)
    file(READ "${lock_file}" _lock)
    string(JSON _n ERROR_VARIABLE _err LENGTH "${_lock}" rexglue_sdk patches)
    if(_err OR _n EQUAL 0)
        return()
    endif()
    math(EXPR _last "${_n} - 1")
    set(_bad "")
    foreach(_i RANGE ${_last})
        string(JSON _patch GET "${_lock}" rexglue_sdk patches ${_i} file)
        string(JSON _nf LENGTH "${_lock}" rexglue_sdk patches ${_i} files)
        math(EXPR _lastf "${_nf} - 1")
        foreach(_j RANGE ${_lastf})
            string(JSON _rel MEMBER "${_lock}" rexglue_sdk patches ${_i} files ${_j})
            string(JSON _want GET "${_lock}" rexglue_sdk patches ${_i} files "${_rel}")
            set(_path "${sdk_dir}/${_rel}")
            if(EXISTS "${_path}")
                file(SHA256 "${_path}" _have)
            else()
                set(_have "missing")
            endif()
            if(NOT _have STREQUAL _want)
                string(APPEND _bad "\n  ${_rel}: ${_have}, expected ${_want} (${_patch})")
            endif()
        endforeach()
    endforeach()
    if(_bad)
        message(FATAL_ERROR
            "x360n: the local SDK patches are not applied to ${sdk_dir}:${_bad}\n"
            "Run: powershell -ExecutionPolicy Bypass -File scripts/setup/apply-sdk-patches.ps1\n"
            "(patches/rexglue-sdk/README.md explains the patches and how to revert them.)")
    endif()
endfunction()

if(CMAKE_SCRIPT_MODE_FILE AND DEFINED SDK_DIR AND DEFINED LOCK_FILE)
    x360n_verify_sdk_patches("${SDK_DIR}" "${LOCK_FILE}")
endif()
