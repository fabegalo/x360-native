# x360-native: supervisor and integration layer on top of ReXGlue.
cmake_minimum_required(VERSION 3.25)
project(x360_native VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ctest must be enabled at the top level to see the SDK's tests.
enable_testing()

# The distributable UI does not link or rebuild the title runtime.
option(X360_DESKTOP_ONLY "Build the desktop distribution without the title SDK runtime" OFF)
if(X360_DESKTOP_ONLY)
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
    add_subdirectory(src/supervisor)
    add_subdirectory(src/content)
    add_subdirectory(src/desktop)
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

add_subdirectory(src/supervisor)
add_subdirectory(src/content)
add_subdirectory(src/desktop)
if(WIN32 AND REXGLUE_USE_D3D12)
    add_subdirectory(src/native)
endif()
if(TARGET Catch2::Catch2WithMain)
    add_subdirectory(tests)
else()
    message(STATUS "x360n tests disabled: Catch2 comes from the SDK only with REXGLUE_BUILD_TESTS=ON")
endif()
