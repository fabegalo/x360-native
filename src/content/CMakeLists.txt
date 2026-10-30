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
if(NOT X360_DESKTOP_ONLY)
add_executable(x360n-content main.cpp xex_patch.cpp xex_crypto.cpp extraction.cpp extraction_manifest.cpp system_update.cpp system_update_probe.cpp update_container.cpp safe_zip.cpp)
target_include_directories(x360n-content PRIVATE "${X360N_UPDATE_ZIP_INCLUDE}")
# disasm: the SDK's binutils PowerPC decoder (thirdparty/disasm), as used by the codegen.
# bcrypt: AES and SHA-1 for apply-patch (Windows CNG).
target_link_libraries(x360n-content PRIVATE rexruntime x360n_core disasm $<$<BOOL:${WIN32}>:bcrypt>)
target_compile_options(x360n-content PRIVATE
    -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
# Runtime DLLs staged on every build, not only when this executable relinks.
x360n_stage_runtime_dlls(x360n-content)
endif()

# AES-128 and SHA-1 for XEX decryption on systems without CNG: the pinned
# SDK's aes_128 (compiled unchanged) and TinySHA1. Windows uses CNG (bcrypt).
add_library(x360n_content_crypto INTERFACE)
if(WIN32)
  target_link_libraries(x360n_content_crypto INTERFACE bcrypt)
else()
  add_library(x360n_content_aes STATIC "${X360_REXGLUE_DIR}/thirdparty/aes_128/aes.c")
  target_include_directories(x360n_content_aes PUBLIC "${X360_REXGLUE_DIR}/thirdparty")
  target_compile_options(x360n_content_aes PRIVATE -w)
  target_link_libraries(x360n_content_crypto INTERFACE x360n_content_aes)
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
  # Only the pinned SDK's container readers and their core utilities. No title,
  # graphics backend, XAM or ReXGlue runtime enters the desktop distribution.
  set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
  add_subdirectory("${X360_REXGLUE_DIR}/thirdparty/spdlog" "${CMAKE_CURRENT_BINARY_DIR}/spdlog")
  if(WIN32)
    set(_host_sources core/mapped_memory_win.cpp core/filesystem_win.cpp core/platform/env_win.cpp core/memory_win.cpp core/math_msvc.cpp)
  else()
    set(_host_sources core/mapped_memory_posix.cpp core/filesystem_posix.cpp core/platform/env_posix.cpp core/memory_posix.cpp core/math_gcc.cpp)
  endif()
  set(_sdk_sources
    filesystem/device.cpp filesystem/entry.cpp
    filesystem/devices/disc_image_device.cpp filesystem/devices/disc_image_entry.cpp filesystem/devices/disc_image_file.cpp
    filesystem/devices/stfs_container_device.cpp filesystem/devices/stfs_container_entry.cpp filesystem/devices/stfs_container_file.cpp
    core/mutex.cpp core/utf8.cpp core/string_buffer.cpp core/filesystem.cpp core/filesystem_wildcard.cpp
    core/logging.cpp core/cvar.cpp core/memory.cpp system/lzx.cpp ${_host_sources})
  list(TRANSFORM _sdk_sources PREPEND "${X360_REXGLUE_DIR}/src/")
  target_sources(x360n-extract PRIVATE ${_sdk_sources})
  target_include_directories(x360n-extract PRIVATE
    "${X360_REXGLUE_DIR}/thirdparty/simde" "${X360_REXGLUE_DIR}/thirdparty/utfcpp/source")
  target_link_libraries(x360n-extract PRIVATE spdlog::spdlog tomlplusplus)
  if(WIN32)
    target_compile_definitions(x360n-extract PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(x360n-extract PRIVATE shell32 ole32)
  else()
    find_package(Threads REQUIRED)
    target_link_libraries(x360n-extract PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
  endif()
  if(APPLE)
    # The SDK's CMake defines this for its own sources; memory_posix.cpp selects
    # its Mach headers before including rex/platform.h.
    target_compile_definitions(x360n-extract PRIVATE REX_PLATFORM_MAC=1)
  endif()
  # The LZX decoder stays a separate, replaceable LGPL library on every system.
  # Original files avoid cabextract's Git symlinks on Windows.
  set(_update_mspack "${X360_REXGLUE_DIR}/thirdparty/libmspack/libmspack/mspack")
  add_library(x360n-mspack SHARED "${_update_mspack}/lzxd.c")
  set_target_properties(x360n-mspack PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
  target_include_directories(x360n-mspack PRIVATE "${_update_mspack}")
  target_link_libraries(x360n-extract PRIVATE x360n-mspack)
  target_include_directories(x360n-extract PRIVATE "${_update_mspack}")
  if(APPLE)
    set_target_properties(x360n-extract PROPERTIES BUILD_RPATH "@loader_path" INSTALL_RPATH "@loader_path")
  elseif(UNIX)
    set_target_properties(x360n-extract PROPERTIES BUILD_RPATH "\$ORIGIN" INSTALL_RPATH "\$ORIGIN")
  endif()
else()
  target_link_libraries(x360n-extract PRIVATE rexruntime)
  target_compile_options(x360n-extract PRIVATE -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
  x360n_stage_runtime_dlls(x360n-extract)
endif()
