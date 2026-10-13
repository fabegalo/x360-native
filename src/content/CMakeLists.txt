# x360n-content: STFS package and disc image info/extraction on top of the SDK's
# StfsContainerDevice and DiscImageDevice (compiled into rexruntime). Separate from x360n so the
# supervisor keeps not linking the runtime (docs/architecture.md, D6).
if(NOT X360_DESKTOP_ONLY)
add_executable(x360n-content main.cpp xex_patch.cpp extraction.cpp extraction_manifest.cpp)
# disasm: the SDK's binutils PowerPC decoder (thirdparty/disasm), as used by the codegen.
# bcrypt: AES and SHA-1 for apply-patch (Windows CNG).
target_link_libraries(x360n-content PRIVATE rexruntime x360n_core disasm bcrypt)
target_compile_options(x360n-content PRIVATE
    -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
# Runtime DLLs staged on every build, not only when this executable relinks.
x360n_stage_runtime_dlls(x360n-content)
endif()

if(WIN32)
  add_executable(x360n-extract extract_main.cpp extraction.cpp extraction_manifest.cpp)
  target_link_libraries(x360n-extract PRIVATE x360n_core)
  if(X360_DESKTOP_ONLY)
    # Only the pinned SDK's container readers and their core utilities. No title,
    # graphics backend, XAM or runtime DLL enters the desktop distribution.
    set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
    add_subdirectory("${X360_REXGLUE_DIR}/thirdparty/spdlog" "${CMAKE_CURRENT_BINARY_DIR}/spdlog")
    set(_sdk_sources
      filesystem/device.cpp filesystem/entry.cpp
      filesystem/devices/disc_image_device.cpp filesystem/devices/disc_image_entry.cpp filesystem/devices/disc_image_file.cpp
      filesystem/devices/stfs_container_device.cpp filesystem/devices/stfs_container_entry.cpp filesystem/devices/stfs_container_file.cpp
      core/mapped_memory_win.cpp core/mutex.cpp core/utf8.cpp core/string_buffer.cpp
      core/filesystem.cpp core/filesystem_win.cpp core/filesystem_wildcard.cpp
      core/logging.cpp core/cvar.cpp core/platform/env_win.cpp core/memory.cpp core/memory_win.cpp)
    list(TRANSFORM _sdk_sources PREPEND "${X360_REXGLUE_DIR}/src/")
    target_sources(x360n-extract PRIVATE ${_sdk_sources})
    target_include_directories(x360n-extract PRIVATE
      "${X360_REXGLUE_DIR}/thirdparty/simde" "${X360_REXGLUE_DIR}/thirdparty/utfcpp/source")
    target_compile_definitions(x360n-extract PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(x360n-extract PRIVATE spdlog::spdlog tomlplusplus shell32 ole32)
  else()
    target_link_libraries(x360n-extract PRIVATE rexruntime)
    target_compile_options(x360n-extract PRIVATE -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
    x360n_stage_runtime_dlls(x360n-extract)
  endif()
endif()
