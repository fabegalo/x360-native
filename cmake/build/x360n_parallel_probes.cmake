# SDL's independent libc probes use the ORIGINAL CMake check modules in bounded
# parallel configure workers. Never guess feature values or replace probe source.
include_guard(GLOBAL)
include(CheckIncludeFile)
include(CheckSymbolExists)
get_filename_component(_X360N_PROBE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

function(x360n_probe_batch kind names headers)
    set(_wanted "")
    foreach(_name IN LISTS names)
        string(TOUPPER "${_name}" _variable)
        string(REGEX REPLACE "[./]" "_" _variable "${_variable}")
        set(_variable "LIBC_HAS_${_variable}")
        if(NOT DEFINED ${_variable})
            list(APPEND _wanted "${_name}")
        endif()
    endforeach()
    if(NOT _wanted)
        return()
    endif()
    string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef _id)
    set(_directory "${CMAKE_BINARY_DIR}/CMakeFiles/x360n-probes-${kind}-${_id}")
    file(MAKE_DIRECTORY "${_directory}")
    set(_context "")
    foreach(_variable CMAKE_C_COMPILER CMAKE_C_COMPILER_ARG1 CMAKE_C_COMPILER_TARGET
            CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN CMAKE_MAKE_PROGRAM CMAKE_BUILD_TYPE
            CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE
            CMAKE_C_FLAGS_RELWITHDEBINFO CMAKE_C_FLAGS_MINSIZEREL
            CMAKE_EXE_LINKER_FLAGS CMAKE_EXE_LINKER_FLAGS_DEBUG CMAKE_EXE_LINKER_FLAGS_RELEASE
            CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO CMAKE_EXE_LINKER_FLAGS_MINSIZEREL
            CMAKE_C_STANDARD CMAKE_C_STANDARD_REQUIRED CMAKE_C_EXTENSIONS
            CMAKE_MSVC_RUNTIME_LIBRARY CMAKE_POSITION_INDEPENDENT_CODE
            CMAKE_TRY_COMPILE_CONFIGURATION CMAKE_TRY_COMPILE_TARGET_TYPE
            CMAKE_REQUIRED_FLAGS CMAKE_REQUIRED_DEFINITIONS CMAKE_REQUIRED_INCLUDES
            CMAKE_REQUIRED_LIBRARIES CMAKE_REQUIRED_LINK_OPTIONS CMAKE_REQUIRED_LINK_DIRECTORIES)
        if(DEFINED ${_variable})
            if("${${_variable}}" MATCHES "\\]==\\]")
                message(FATAL_ERROR "Unsupported bracket delimiter in probe context ${_variable}")
            endif()
            string(APPEND _context "set(${_variable} [==[${${_variable}}]==] CACHE STRING \"Inherited native probe context\" FORCE)\n")
        endif()
    endforeach()
    file(WRITE "${_directory}/context.cmake" "${_context}set(PROBE_KIND ${kind})\nset(PROBE_NAMES [==[${_wanted}]==])\nset(PROBE_HEADERS [==[${headers}]==])\n")
    message(STATUS "x360n: checking independent libc ${kind} in parallel (original CMake probes)")
    execute_process(COMMAND powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass
        -File "${_X360N_PROBE_ROOT}/scripts/build/parallel-probes.ps1"
        -CMake "${CMAKE_COMMAND}" -Source "${_X360N_PROBE_ROOT}/cmake/build/probes"
        -Directory "${_directory}" RESULT_VARIABLE _result TIMEOUT 660)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Parallel libc probe failed (${_result}); evidence preserved in ${_directory}")
    endif()
    include("${_directory}/results.cmake")
endfunction()

macro(check_include_file header variable)
    if(CMAKE_CURRENT_SOURCE_DIR STREQUAL "${REXSDK_DIR}/thirdparty/sdl3" AND
       DEFINED headers_to_check AND "${header}" IN_LIST headers_to_check AND NOT _X360N_HEADER_BATCH_DONE)
        set(_X360N_HEADER_BATCH_DONE TRUE)
        x360n_probe_batch(headers "${headers_to_check}" "")
    endif()
    _check_include_file("${header}" "${variable}" ${ARGN})
endmacro()

macro(check_symbol_exists symbol headers variable)
    if(CMAKE_CURRENT_SOURCE_DIR STREQUAL "${REXSDK_DIR}/thirdparty/sdl3" AND
       DEFINED symbols_to_check AND "${symbol}" IN_LIST symbols_to_check AND NOT _X360N_SYMBOL_BATCH_DONE)
        set(_X360N_SYMBOL_BATCH_DONE TRUE)
        x360n_probe_batch(symbols "${symbols_to_check}" "${headers}")
    endif()
    _check_symbol_exists("${symbol}" "${headers}" "${variable}")
endmacro()
