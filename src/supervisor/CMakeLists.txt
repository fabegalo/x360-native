# x360n supervisor. Uses SDK headers (XEX structures from rex/system/util/xex2_info.h,
# CLI11) and fmt; does not link the ReXGlue runtime.
add_library(x360n_core STATIC
    sha256.cpp
    xex_identity.cpp
    paths.cpp
    child_process.cpp
    report.cpp
)
target_include_directories(x360n_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(x360n_core SYSTEM PUBLIC
    ${X360_REXGLUE_DIR}/include
    ${X360_REXGLUE_DIR}/thirdparty/cli11/include)
target_link_libraries(x360n_core PUBLIC fmt::fmt)
if(WIN32)
    # dbghelp: stack walk of a crashed child (x360n run --crash-stack); part of Windows.
    target_link_libraries(x360n_core PUBLIC dbghelp)
endif()
target_compile_options(x360n_core PUBLIC -Wall -Wextra)
target_compile_definitions(x360n_core PUBLIC
    X360N_VERSION="${PROJECT_VERSION}"
    X360N_BUILD_CONFIG="$<CONFIG>"
    X360N_SDK_COMMIT_LOCKED="${X360_SDK_COMMIT_LOCKED}"
    X360N_SDK_TAG_LOCKED="${X360_SDK_TAG_LOCKED}"
    X360N_LLVM_VERSION_LOCKED="${X360_LLVM_VERSION_LOCKED}")

add_executable(x360n main.cpp)
target_link_libraries(x360n PRIVATE x360n_core)
