# x360-native: supervisor and integration layer on top of ReXGlue.
cmake_minimum_required(VERSION 3.25)
project(x360_native VERSION 0.1.0 LANGUAGES C CXX)
include(cmake/distribution/x360n_version.cmake)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ctest must be enabled at the top level to see the SDK's tests.
enable_testing()

# A dependency-light contract build exercises the same production process
# library on native hosts without building a GPU runtime or launching a title.
option(X360_PLATFORM_CONTRACTS_ONLY "Build platform process contracts only" OFF)
if(X360_PLATFORM_CONTRACTS_ONLY)
    if(X360_DISTRIBUTION OR X360_DESKTOP_ONLY OR X360_SUPERVISOR_ONLY)
        message(FATAL_ERROR "Platform contracts are a separate diagnostic build, not a desktop distribution")
    endif()
    set(X360_REXGLUE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/rexglue-sdk" CACHE PATH "Pinned SDK dependency sources")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/deps.lock.json" _platform_lock)
    string(JSON _platform_sdk_commit GET "${_platform_lock}" rexglue_sdk commit)
    execute_process(COMMAND git -C "${X360_REXGLUE_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE _platform_actual_commit OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _platform_git_rc)
    if(NOT _platform_git_rc EQUAL 0 OR NOT _platform_actual_commit STREQUAL _platform_sdk_commit)
        message(FATAL_ERROR "Platform contracts require dependency sources from the pinned SDK")
    endif()
    add_subdirectory("${X360_REXGLUE_DIR}/thirdparty/fmt" platform-deps/fmt)
    add_subdirectory("${X360_REXGLUE_DIR}/thirdparty/catch2" platform-deps/catch2)
    add_subdirectory(src/platform)
    add_subdirectory(tests/platform)
    return()
endif()

# The distributable UI does not link or rebuild the title runtime.
option(X360_DESKTOP_ONLY "Build the desktop distribution without the title SDK runtime" OFF)
option(X360_SUPERVISOR_ONLY "Build the shared supervisor and preparation model without GPU/runtime" OFF)
option(X360_DISTRIBUTION "Build the end-user distribution (Release only)" OFF)
if(X360_SUPERVISOR_ONLY AND (X360_DESKTOP_ONLY OR X360_DISTRIBUTION))
    message(FATAL_ERROR "Supervisor-only is a separate diagnostic build, not a desktop distribution")
endif()
if(X360_DISTRIBUTION)
    get_property(_distribution_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(NOT X360_DESKTOP_ONLY OR
       (_distribution_multi AND NOT CMAKE_CONFIGURATION_TYPES STREQUAL "Release") OR
       (NOT _distribution_multi AND NOT CMAKE_BUILD_TYPE STREQUAL "Release"))
        message(FATAL_ERROR "End-user distribution requires desktop-only Release configuration")
    endif()
endif()
if(X360_DESKTOP_ONLY OR X360_SUPERVISOR_ONLY)
    if(X360_DISTRIBUTION)
        set(CMAKE_C_COMPILER_LAUNCHER "${CMAKE_COMMAND};-P;${CMAKE_CURRENT_SOURCE_DIR}/cmake/distribution/x360n_distribution_compile.cmake;--")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CMAKE_C_COMPILER_LAUNCHER}")
    endif()
    include(cmake/build/x360n_build_isolation.cmake)
    x360n_guard_ninja()
    set(X360_REXGLUE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/rexglue-sdk" CACHE PATH "Pinned SDK dependency sources")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/deps.lock.json" X360_LOCK)
    string(JSON X360_SDK_COMMIT_LOCKED GET "${X360_LOCK}" rexglue_sdk commit)
    string(JSON X360_SDK_TAG_LOCKED GET "${X360_LOCK}" rexglue_sdk tag)
    string(JSON X360_LLVM_VERSION_LOCKED GET "${X360_LOCK}" llvm version)
    execute_process(COMMAND git -C "${X360_REXGLUE_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE _desktop_sdk_commit OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _desktop_git_rc)
    if(NOT _desktop_git_rc EQUAL 0 OR NOT _desktop_sdk_commit STREQUAL X360_SDK_COMMIT_LOCKED)
        message(FATAL_ERROR "Desktop dependencies must come from the pinned SDK revision")
    endif()
    add_subdirectory("${X360_REXGLUE_DIR}/thirdparty/fmt" desktop-deps/fmt)
    add_subdirectory("${X360_REXGLUE_DIR}/thirdparty/tomlplusplus" desktop-deps/toml)
    add_library(tomlplusplus INTERFACE)
    target_link_libraries(tomlplusplus INTERFACE tomlplusplus::tomlplusplus)
    add_subdirectory("${X360_REXGLUE_DIR}/thirdparty/catch2" desktop-deps/catch2)
    add_subdirectory(src/platform)
    add_subdirectory(src/supervisor)
    if(NOT X360_SUPERVISOR_ONLY)
        add_subdirectory(src/content)
    endif()
    add_subdirectory(src/desktop)
    if(X360_SUPERVISOR_ONLY)
        include(tests/supervisor/native_targets.cmake)
    endif()
    return()
endif()

# ReXGlue consumed in its documented "Git Submodule" mode (wiki: SDK-Install-Modes).
# Pinned revision: deps.lock.json / .gitmodules. We do not run the SDK install
# target: it writes HKCU\Software\Kitware\CMake\Packages\rexglue.
set(X360_REXGLUE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/rexglue-sdk")
if(NOT EXISTS "${X360_REXGLUE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "ReXGlue SDK missing at ${X360_REXGLUE_DIR}. Run: git submodule update --init --recursive")
endif()
add_subdirectory("${X360_REXGLUE_DIR}" rexglue-sdk)

# The lock file is the single source of truth for pinned revisions.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/deps.lock.json" X360_LOCK)
string(JSON X360_SDK_COMMIT_LOCKED GET "${X360_LOCK}" rexglue_sdk commit)
string(JSON X360_SDK_TAG_LOCKED GET "${X360_LOCK}" rexglue_sdk tag)
string(JSON X360_LLVM_VERSION_LOCKED GET "${X360_LOCK}" llvm version)
execute_process(
    COMMAND git -C "${X360_REXGLUE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE X360_SDK_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _git_rc)
if(NOT _git_rc EQUAL 0 OR NOT X360_SDK_COMMIT STREQUAL X360_SDK_COMMIT_LOCKED)
    message(FATAL_ERROR "external/rexglue-sdk is at '${X360_SDK_COMMIT}', deps.lock.json pins "
                        "'${X360_SDK_COMMIT_LOCKED}'. Update the lock file deliberately or check out the pin.")
endif()

# Backports tied to the pinned commit (shared with title projects under titles/).
include(cmake/sdk/x360n_sdk_backports.cmake)
x360n_apply_sdk_backports("${X360_REXGLUE_DIR}")

add_subdirectory(src/platform)
add_subdirectory(src/supervisor)
add_subdirectory(src/content)
add_subdirectory(src/desktop)
# Native: D3D12 (+ Vulkan) on Windows, Vulkan elsewhere.
if((WIN32 AND REXGLUE_USE_D3D12) OR (NOT WIN32 AND REXGLUE_USE_VULKAN))
    add_subdirectory(src/native)
endif()
if(TARGET Catch2::Catch2WithMain)
    add_subdirectory(tests)
else()
    message(STATUS "x360n tests disabled: Catch2 comes from the SDK only with REXGLUE_BUILD_TESTS=ON")
endif()
