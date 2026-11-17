# Content identity, catalog and AOT project generation are shared by the launcher
# and supervisor without linking the title runtime.
add_library(x360n_catalog STATIC catalog.cpp library.cpp identify.cpp import_source.cpp imported_games.cpp generic_aot.cpp system_update_probe.cpp)
target_include_directories(x360n_catalog PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(x360n_catalog PRIVATE ${X360_REXGLUE_DIR}/thirdparty/inja/third_party/include)
target_link_libraries(x360n_catalog PUBLIC x360n_core PRIVATE tomlplusplus)
target_compile_definitions(x360n_catalog PRIVATE REX_PHYSICAL_OFFSET_IN_CODE=$<BOOL:${REX_PHYSICAL_OFFSET_IN_CODE}>)
if(X360_SUPERVISOR_ONLY)
  return()
endif()

# x360n-content: STFS package and disc image info/extraction on top of the SDK's
# StfsContainerDevice and DiscImageDevice (compiled into rexruntime). Separate from x360n so the
# supervisor keeps not linking the runtime (docs/architecture.md, D6).
# SDL's pinned miniz header selects compression-only APIs with fixed defines.
# Configure just those feature switches in the private build directory; keep
# the SDK untouched and preserve the original implementation/license verbatim.
file(READ "${X360_REXGLUE_DIR}/thirdparty/sdl3/src/video/miniz.h" _update_miniz)
string(REPLACE "#define MINIZ_NO_INFLATE_APIS\n" "" _update_miniz "${_update_miniz}")
string(REPLACE "#define MINIZ_NO_ARCHIVE_APIS\n" "" _update_miniz "${_update_miniz}")
string(REPLACE "#define MINIZ_NO_ZLIB_APIS\n" "" _update_miniz "${_update_miniz}")
set(X360N_UPDATE_ZIP_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/system-update" CACHE INTERNAL "Private configured miniz header")
file(GENERATE OUTPUT "${X360N_UPDATE_ZIP_INCLUDE}/miniz.h" CONTENT "${_update_miniz}")
# XEX2 keys come from the pinned SDK loader, never from this repository, so the
# extractor can read a title's own name and icon as the runtime would.
file(READ "${X360_REXGLUE_DIR}/src/filesystem/xex.cpp" _xex_loader)
if(NOT _xex_loader MATCHES "kMasterKeys = \\{\\{[ \t\r\n]*\\{([^}]*)\\},[ \t\r\n]*\\{\\}")
  message(FATAL_ERROR "Expected public retail key and zero devkit default in the pinned SDK loader")
endif()
string(REGEX REPLACE "[ \t\r\n]+" " " _key "${CMAKE_MATCH_1}")
set(_xex_keys "#pragma once\n#include <array>\n#include <cstdint>\nnamespace x360n::content {\ninline constexpr std::array<uint8_t, 16> kSdkXex2RetailKey{${_key}};\ninline constexpr std::array<uint8_t, 16> kSdkXex2DevkitKey{};\n}\n")
file(GENERATE OUTPUT "${X360N_UPDATE_ZIP_INCLUDE}/xex_sdk_keys.h" CONTENT "${_xex_keys}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${X360_REXGLUE_DIR}/src/filesystem/xex.cpp")
if(NOT X360_DESKTOP_ONLY)
add_executable(x360n-content main.cpp xex_patch.cpp xex_crypto.cpp extraction.cpp extraction_manifest.cpp system_update.cpp system_update_probe.cpp update_container.cpp safe_zip.cpp)
target_include_directories(x360n-content PRIVATE "${X360N_UPDATE_ZIP_INCLUDE}" "${X360_REXGLUE_DIR}/src")
# disasm: the SDK's binutils PowerPC decoder (thirdparty/disasm), as used by the codegen.
# bcrypt: AES and SHA-1 for apply-patch (Windows CNG).
target_link_libraries(x360n-content PRIVATE ${X360N_SDK_LIBRARIES} rexcodegen x360n_core disasm $<$<BOOL:${WIN32}>:bcrypt>)
target_compile_options(x360n-content PRIVATE
    -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
# Runtime DLLs staged on every build, not only when this executable relinks.
x360n_stage_runtime_dlls(x360n-content)
endif()

# AES-128 and SHA-1 use the pinned SDK's ExCrypt on systems without CNG.
add_library(x360n_content_crypto INTERFACE)
if(WIN32)
  target_link_libraries(x360n_content_crypto INTERFACE bcrypt)
else()
  target_link_libraries(x360n_content_crypto INTERFACE excrypt)
endif()
if(TARGET x360n-content)
  target_link_libraries(x360n-content PRIVATE x360n_content_crypto)
endif()
# The extractor runs on every system: ISO/GDFX, STFS/PIRS and System Update
# import share one implementation.
add_executable(x360n-extract extract_main.cpp extraction.cpp extraction_manifest.cpp system_update.cpp system_update_probe.cpp update_container.cpp safe_zip.cpp xex_patch.cpp xex_crypto.cpp)
target_include_directories(x360n-extract PRIVATE "${X360N_UPDATE_ZIP_INCLUDE}")
target_link_libraries(x360n-extract PRIVATE x360n_core x360n_content_crypto)
if(X360_DESKTOP_ONLY)
  # Canonical SDK modules, statically linked without title or GPU services.
  target_link_libraries(x360n-extract PRIVATE rexbase rexfilesystem)
  target_compile_definitions(x360n-extract PRIVATE X360N_STATIC_FILESYSTEM)
  rexglue_link_host_allocator(x360n-extract)
  set_target_properties(x360n-mspack PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  if(APPLE)
    set_target_properties(x360n-extract PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
  elseif(UNIX)
    set_target_properties(x360n-extract PROPERTIES BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
  else()
    # UTF-8 process code page (Windows 10 1903+), the manifest the full build
    # gets from x360n_stage_runtime_dlls: narrow paths and messages carry any
    # Unicode folder name without changing the system locale.
    target_link_options(x360n-extract PRIVATE "LINKER:/MANIFESTINPUT:${PROJECT_SOURCE_DIR}/cmake/runtime/x360n_runtime.manifest")
    set_property(TARGET x360n-extract APPEND PROPERTY LINK_DEPENDS "${PROJECT_SOURCE_DIR}/cmake/runtime/x360n_runtime.manifest")
  endif()
else()
  target_link_libraries(x360n-extract PRIVATE rexbase rexfilesystem)
  target_compile_options(x360n-extract PRIVATE -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
  x360n_stage_runtime_dlls(x360n-extract)
endif()
