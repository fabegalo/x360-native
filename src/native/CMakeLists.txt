# Share shader semantics and packet/register services, never the Xenos executor.
set(_gpu ${X360_REXGLUE_DIR}/src/graphics)
add_library(rexgpu-native SHARED quality/settings.cpp commands/plugin.cpp commands/command_processor.cpp commands/packet_guard.cpp d3d12/d3d12_scanout.cpp textures/texture.cpp textures/texture_load.cpp memory/guest_memory_mirror.cpp memory/scaled_resolve.cpp
    ${_gpu}/graphics_system.cpp ${_gpu}/command_processor.cpp
    ${_gpu}/register_file.cpp ${_gpu}/registers.cpp ${_gpu}/flags.cpp
    pipeline/shader_cache.cpp pipeline/pipeline_store.cpp d3d12/d3d12_render.cpp d3d12/d3d12_draw.cpp shaders/rectangle_shader.cpp shaders/point_shader.cpp d3d12/d3d12_resolve.cpp d3d12/d3d12_textures.cpp d3d12/edram_format.cpp ${_gpu}/util/draw.cpp
    ${_gpu}/pipeline/shader/shader.cpp ${_gpu}/format/ucode.cpp
    ${_gpu}/pipeline/shader/translator.cpp ${_gpu}/pipeline/shader/translator_disasm.cpp
    ${_gpu}/pipeline/shader/dxbc.cpp ${_gpu}/pipeline/shader/dxbc_translator.cpp
    ${_gpu}/pipeline/shader/dxbc_translator_alu.cpp ${_gpu}/pipeline/shader/dxbc_translator_fetch.cpp
    ${_gpu}/pipeline/shader/dxbc_translator_memexport.cpp ${_gpu}/pipeline/shader/dxbc_translator_om.cpp
    ${X360_REXGLUE_DIR}/thirdparty/dxbc/DXBCChecksum.cpp)
target_link_libraries(rexgpu-native PRIVATE rexruntime xxHash::xxhash d3d12 dxgi dxguid d3dcompiler)
target_include_directories(rexgpu-native PRIVATE ${X360_REXGLUE_DIR})
set_target_properties(rexgpu-native PROPERTIES DEBUG_POSTFIX d RELWITHDEBINFO_POSTFIX rd)
target_compile_options(rexgpu-native PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_compile_definitions(rexgpu-native PRIVATE REX_GPU_VALIDATE_PACKET=1)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/shaders/scanout.hlsl NATIVE_SCANOUT_HLSL)
configure_file(shaders/scanout_source.h.in scanout_source.h @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS shaders/scanout.hlsl)
target_include_directories(rexgpu-native PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(rexgpu-native PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
if(REXGLUE_USE_VULKAN)
  target_compile_definitions(rexgpu-native PRIVATE X360N_NATIVE_VULKAN=1)
  # Native owns execution/resources; share only the guest shader translator.
  # Manual backend selection is explicit; launcher availability requires title validation.
  set(_native_vulkan_libraries Vulkan::Headers glslang::SPIRV glslang
      MachineIndependent GenericCodeGen OSDependent OGLCompiler)
  target_sources(rexgpu-native PRIVATE vulkan/vulkan_core.cpp vulkan/vulkan_scanout.cpp
      vulkan/vulkan_shader_cache.cpp vulkan/vulkan_render.cpp vulkan/vulkan_draw.cpp
      vulkan/vulkan_resolve.cpp vulkan/vulkan_textures.cpp vulkan/vulkan_backend.cpp
      ${_gpu}/pipeline/shader/spirv_builder.cpp ${_gpu}/pipeline/shader/spirv.cpp
      ${_gpu}/pipeline/shader/spirv_translator.cpp ${_gpu}/pipeline/shader/spirv_translator_alu.cpp
      ${_gpu}/pipeline/shader/spirv_translator_fetch.cpp ${_gpu}/pipeline/shader/spirv_translator_memexport.cpp
      ${_gpu}/pipeline/shader/spirv_translator_rb.cpp)
  target_link_libraries(rexgpu-native PRIVATE ${_native_vulkan_libraries})
endif()
if(X360N_NATIVE_PLUGIN_ONLY)
  return()
endif()
if(REXGLUE_USE_VULKAN)
  # Opt-in headless benchmark of the real implementation, with internal C++
  # classes linked locally rather than enlarging the GPU plugin's public ABI.
  get_target_property(_native_perf_sources rexgpu-native SOURCES)
  get_target_property(_native_perf_includes rexgpu-native INCLUDE_DIRECTORIES)
  get_target_property(_native_perf_defines rexgpu-native COMPILE_DEFINITIONS)
  get_target_property(_native_perf_libraries rexgpu-native LINK_LIBRARIES)
  add_executable(x360n_native_perf_probe EXCLUDE_FROM_ALL ../../tests/native/perf_probe.cpp ${_native_perf_sources})
  target_include_directories(x360n_native_perf_probe PRIVATE ${_native_perf_includes})
  target_compile_definitions(x360n_native_perf_probe PRIVATE ${_native_perf_defines})
  target_link_libraries(x360n_native_perf_probe PRIVATE ${_native_perf_libraries})
  x360n_stage_runtime_dlls(x360n_native_perf_probe)
endif()
add_executable(x360n_native_probe ../../tests/native/probe.cpp)
target_link_libraries(x360n_native_probe PRIVATE rexruntime windowscodecs ole32 d3d12)
x360n_stage_runtime_dlls(x360n_native_probe PLUGIN_TARGETS rexgpu-native rexgpu-xenos)
add_executable(x360n_native_occlusion_probe ../../tests/native/occlusion_probe.cpp)
target_link_libraries(x360n_native_occlusion_probe PRIVATE rexruntime d3d12)
x360n_stage_runtime_dlls(x360n_native_occlusion_probe PLUGIN_TARGETS rexgpu-native)
add_test(NAME sdkcheck.Native-ROV-sample-counts COMMAND x360n_native_occlusion_probe
    ${CMAKE_CURRENT_BINARY_DIR}/occlusion-probe)
set_tests_properties(sdkcheck.Native-ROV-sample-counts PROPERTIES LABELS project TIMEOUT 60)
add_executable(x360n_native_memexport_probe ../../tests/native/memexport_probe.cpp memory/guest_memory_mirror.cpp)
target_link_libraries(x360n_native_memexport_probe PRIVATE rexruntime d3d12)
x360n_stage_runtime_dlls(x360n_native_memexport_probe PLUGIN_TARGETS rexgpu-native)
add_test(NAME sdkcheck.Native-MEMEXPORT-d3d12 COMMAND x360n_native_memexport_probe
    ${CMAKE_CURRENT_BINARY_DIR}/memexport-d3d12 d3d12)
set_tests_properties(sdkcheck.Native-MEMEXPORT-d3d12 PROPERTIES LABELS project TIMEOUT 90)
if(REXGLUE_USE_VULKAN)
  add_test(NAME sdkcheck.Native-MEMEXPORT-vulkan COMMAND x360n_native_memexport_probe
      ${CMAKE_CURRENT_BINARY_DIR}/memexport-vulkan vulkan)
  set_tests_properties(sdkcheck.Native-MEMEXPORT-vulkan PROPERTIES LABELS project TIMEOUT 90)
  add_test(NAME sdkcheck.Native-FSI-sample-counts COMMAND x360n_native_occlusion_probe
      ${CMAKE_CURRENT_BINARY_DIR}/occlusion-probe-vulkan vulkan)
  set_tests_properties(sdkcheck.Native-FSI-sample-counts PROPERTIES LABELS project TIMEOUT 60)
  add_executable(x360n_native_vulkan_probe ../../tests/native/vulkan_probe.cpp
      vulkan/vulkan_core.cpp vulkan/vulkan_scanout.cpp)
  target_include_directories(x360n_native_vulkan_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(x360n_native_vulkan_probe PRIVATE rexruntime ${_native_vulkan_libraries})
  x360n_stage_runtime_dlls(x360n_native_vulkan_probe)
  add_test(NAME native.vulkan-components COMMAND x360n_native_vulkan_probe
      ${CMAKE_CURRENT_BINARY_DIR}/vulkan-components)
  set_tests_properties(native.vulkan-components PROPERTIES LABELS project TIMEOUT 60)
endif()
if(TARGET Catch2::Catch2WithMain)
  add_executable(x360n_native_tests ../../tests/native/texture_test.cpp ../../tests/native/packet_test.cpp ../../tests/native/rectangle_test.cpp ../../tests/native/point_test.cpp ../../tests/native/texture_load_test.cpp textures/texture.cpp textures/texture_load.cpp memory/guest_memory_mirror.cpp memory/scaled_resolve.cpp commands/packet_guard.cpp shaders/rectangle_shader.cpp shaders/point_shader.cpp ${_gpu}/util/draw.cpp ${_gpu}/register_file.cpp ${_gpu}/registers.cpp ${_gpu}/flags.cpp ${X360_REXGLUE_DIR}/thirdparty/dxbc/DXBCChecksum.cpp)
  target_include_directories(x360n_native_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${X360_REXGLUE_DIR} ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
  target_link_libraries(x360n_native_tests PRIVATE rexruntime Catch2::Catch2WithMain d3d12 d3dcompiler)
  target_sources(x360n_native_tests PRIVATE ../../tests/native/pipeline_store_test.cpp pipeline/pipeline_store.cpp)
  target_sources(x360n_native_tests PRIVATE ../../tests/native/occlusion_test.cpp)
  target_link_libraries(x360n_native_tests PRIVATE xxHash::xxhash)
  x360n_stage_runtime_dlls(x360n_native_tests)
  add_test(NAME native.texture COMMAND x360n_native_tests ~[native-packet])
  # Includes exhaustive GPU/CPU texture comparisons, also in unoptimized Debug.
  set_tests_properties(native.texture PROPERTIES LABELS project TIMEOUT 180)
  add_test(NAME native.packet-payload COMMAND x360n_native_tests [native-packet])
  set_tests_properties(native.packet-payload PROPERTIES LABELS project TIMEOUT 30)
  add_test(NAME native.occlusion-contract COMMAND x360n_native_tests [native-zpd])
  set_tests_properties(native.occlusion-contract PROPERTIES LABELS project TIMEOUT 30)
  add_test(NAME native.abi COMMAND x360n_native_probe native ${CMAKE_CURRENT_BINARY_DIR}/abi-probe --abi)
  set_tests_properties(native.abi PROPERTIES LABELS project TIMEOUT 20)
  # D1OVL plane composed before gamma, programmed directly and through VdSwap's
  # system command buffer (PM4_XE_SYSTEM_COMMANDS), against computed pixels.
  set(_overlay_backends native xenos:d3d12)
  if(REXGLUE_USE_VULKAN)
    list(APPEND _overlay_backends xenos:vulkan)
  endif()
  foreach(_backend IN LISTS _overlay_backends)
    string(REPLACE ":" "-" _dir "${_backend}")
    foreach(_mode overlay overlay-system)
      add_test(NAME native.${_mode}-${_dir} COMMAND x360n_native_probe ${_backend}
          ${CMAKE_CURRENT_BINARY_DIR}/${_mode}-${_dir} --${_mode})
      set_tests_properties(native.${_mode}-${_dir} PROPERTIES LABELS project TIMEOUT 60)
    endforeach()
  endforeach()
  add_test(NAME sdkcheck.Native-packet-guard COMMAND ${CMAKE_COMMAND}
      -DPROBE=$<TARGET_FILE:x360n_native_probe> -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/guard-probe
      -P ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/native/reject_packet.cmake)
  set_tests_properties(sdkcheck.Native-packet-guard PROPERTIES LABELS project TIMEOUT 25)
  # Patches 0077 (D3D12) and 0145 (Vulkan): a synthetic draw whose invalid vertex
  # fetch p0 predicates out is issued; the same fetch made active is rejected.
  set(_fetch_apis d3d12)
  if(REXGLUE_USE_VULKAN)
    list(APPEND _fetch_apis vulkan)
  endif()
  foreach(_api IN LISTS _fetch_apis)
    string(REPLACE "d3d12" "D3D12" _name "${_api}")
    string(REPLACE "vulkan" "Vulkan" _name "${_name}")
    add_test(NAME sdkcheck.${_name}-predicated-vertex-fetch COMMAND x360n_native_probe xenos:${_api}
        ${CMAKE_CURRENT_BINARY_DIR}/predicated-fetch-${_api} --predicated-fetch)
    set_tests_properties(sdkcheck.${_name}-predicated-vertex-fetch PROPERTIES LABELS project TIMEOUT 60)
  endforeach()
  # Patch 0142: non-memexport Xenos draws must not wait for the cvar registry lock.
  # native.edram-barriers: Native EDRAM barriers only on ROV layout changes.
  # Replays a retained draw from local evidence (user content, not distributed), so
  # the checks exist only where that snapshot does.
  set(X360N_DRAW_SNAPSHOT "${CMAKE_SOURCE_DIR}/.local/evidence/validation/native-n4/isolated-draw"
      CACHE PATH "Retained draw snapshot for the Xenos draw probes")
  if(EXISTS "${X360N_DRAW_SNAPSHOT}/draw.txt")
    set(_xenos_apis d3d12)
    if(REXGLUE_USE_VULKAN)
      list(APPEND _xenos_apis vulkan)
    endif()
    foreach(_api IN LISTS _xenos_apis)
      string(REPLACE "d3d12" "D3D12" _name "${_api}")
      string(REPLACE "vulkan" "Vulkan" _name "${_name}")
      add_test(NAME sdkcheck.${_name}-draw-registry-lock COMMAND x360n_native_probe xenos:${_api}
          ${CMAKE_CURRENT_BINARY_DIR}/registry-lock-${_api} --draw-registry-locked ${X360N_DRAW_SNAPSHOT})
      set_tests_properties(sdkcheck.${_name}-draw-registry-lock PROPERTIES LABELS project TIMEOUT 60)
    endforeach()
    add_test(NAME native.edram-barriers COMMAND ${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:x360n_native_probe>
        -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/edram-barriers -DSNAPSHOT=${X360N_DRAW_SNAPSHOT}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/native/edram_barriers.cmake)
    set_tests_properties(native.edram-barriers PROPERTIES LABELS project TIMEOUT 150)
    # Patch 0144: a system command buffer's fence is visible only after its resolves.
    add_test(NAME sdkcheck.Native-system-commands COMMAND x360n_native_probe native
        ${CMAKE_CURRENT_BINARY_DIR}/system-resolve --draw-system-resolve ${X360N_DRAW_SNAPSHOT})
    set_tests_properties(sdkcheck.Native-system-commands PROPERTIES LABELS project TIMEOUT 60)
    if(REXGLUE_USE_VULKAN)
      add_test(NAME sdkcheck.Vulkan-texture-exp-adjust COMMAND ${CMAKE_COMMAND}
          -DPROBE=$<TARGET_FILE:x360n_native_probe> -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/api-capture
          -DSNAPSHOT=${X360N_DRAW_SNAPSHOT} -P ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/native/api_capture_match.cmake)
      set_tests_properties(sdkcheck.Vulkan-texture-exp-adjust PROPERTIES LABELS project TIMEOUT 150)
    endif()
  endif()
endif()
