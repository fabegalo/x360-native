# Included from tests/CMakeLists.txt; paths and target outputs stay rooted there.
add_executable(x360n_resolve_checks sdkcheck/resolve_empty_probe.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/draw_state.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/../../xenos/register_file.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/../../xenos/register.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/xenos_settings.h)
target_link_libraries(x360n_resolve_checks PRIVATE ${X360N_SDK_LIBRARIES})
target_include_directories(x360n_resolve_checks PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
x360n_stage_runtime_dlls(x360n_resolve_checks)
add_test(NAME sdkcheck.empty-resolve COMMAND x360n_resolve_checks)
set_tests_properties(sdkcheck.empty-resolve PROPERTIES LABELS project TIMEOUT 30)

# patch 0087: s5.10 fixed-point render targets and raw signed-integer resolves.
add_executable(x360n_fixed16_resolve_checks sdkcheck/fixed16_resolve_probe.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/draw_state.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/../../xenos/register_file.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/../../xenos/register.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/xenos_settings.h)
target_link_libraries(x360n_fixed16_resolve_checks PRIVATE ${X360N_SDK_LIBRARIES})
target_include_directories(x360n_fixed16_resolve_checks PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
x360n_stage_runtime_dlls(x360n_fixed16_resolve_checks)
add_test(NAME sdkcheck.fixed16-resolve COMMAND x360n_fixed16_resolve_checks)
set_tests_properties(sdkcheck.fixed16-resolve PROPERTIES LABELS project TIMEOUT 30)

# Conservative invalid-descriptor proof, synthetic microcode only.
add_executable(x360n_inactive_fetch_checks sdkcheck/inactive_vertex_fetch_probe.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/../../xenos/register_file.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/../../xenos/register.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/xenos_settings.h
    ${X360_REXGLUE_DIR}/src/gpu/xenos/../../xenos/ucode.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/pipeline/shader/shader.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/pipeline/shader/translator.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/pipeline/shader/translator_disasm.cpp)
target_link_libraries(x360n_inactive_fetch_checks PRIVATE ${X360N_SDK_LIBRARIES})
target_include_directories(x360n_inactive_fetch_checks PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
x360n_stage_runtime_dlls(x360n_inactive_fetch_checks)
add_test(NAME sdkcheck.inactive-vertex-fetch COMMAND x360n_inactive_fetch_checks)
set_tests_properties(sdkcheck.inactive-vertex-fetch PROPERTIES LABELS project TIMEOUT 30)

# A separate staging producer keeps the CPU-only checks independent of GPU
# plugins staged by other tests in the parent directory.
add_subdirectory(sdkcheck/g3)

# patch 0183: Canary guest texture addressing (packed tails, base map pages).
add_executable(x360n_canary_texture_layout_checks sdkcheck/canary_texture_layout_probe.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/../../xenos/texture_layout.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/../../xenos/texture_info.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/../../xenos/texture_info_format.cpp)
target_link_libraries(x360n_canary_texture_layout_checks PRIVATE ${X360N_SDK_LIBRARIES})
x360n_stage_runtime_dlls(x360n_canary_texture_layout_checks)
add_test(NAME sdkcheck.canary-texture-layout COMMAND x360n_canary_texture_layout_checks)
set_tests_properties(sdkcheck.canary-texture-layout PROPERTIES LABELS project TIMEOUT 30)

# patch 0184: Canary 8-bit texture / resolve format aliases.
add_executable(x360n_canary_format_alias_checks sdkcheck/canary_format_alias_probe.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/draw_state.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/../../xenos/register_file.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/../../xenos/register.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/xenos_settings.h)
target_link_libraries(x360n_canary_format_alias_checks PRIVATE ${X360N_SDK_LIBRARIES})
target_include_directories(x360n_canary_format_alias_checks PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
x360n_stage_runtime_dlls(x360n_canary_format_alias_checks)
add_test(NAME sdkcheck.canary-format-alias COMMAND x360n_canary_format_alias_checks)
set_tests_properties(sdkcheck.canary-format-alias PROPERTIES LABELS project TIMEOUT 30)

# patch 0186: Canary guest texture layout and exact tiled 3D bounds.
add_executable(x360n_canary_texture_bounds_checks sdkcheck/canary_texture_bounds_probe.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/../../xenos/texture_layout.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/../../xenos/texture_info.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/../../xenos/texture_info_format.cpp)
target_link_libraries(x360n_canary_texture_bounds_checks PRIVATE ${X360N_SDK_LIBRARIES})
x360n_stage_runtime_dlls(x360n_canary_texture_bounds_checks)
add_test(NAME sdkcheck.canary-texture-bounds COMMAND x360n_canary_texture_bounds_checks)
set_tests_properties(sdkcheck.canary-texture-bounds PROPERTIES LABELS project TIMEOUT 60)

# patch 0187: Canary tiled resolve destination base phase below 32bpp.
add_executable(x360n_canary_resolve_dest_checks sdkcheck/canary_resolve_dest_probe.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/draw_state.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/../../xenos/register_file.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/../../xenos/register.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/xenos_settings.h)
target_link_libraries(x360n_canary_resolve_dest_checks PRIVATE ${X360N_SDK_LIBRARIES})
target_include_directories(x360n_canary_resolve_dest_checks PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
x360n_stage_runtime_dlls(x360n_canary_resolve_dest_checks)
add_test(NAME sdkcheck.canary-resolve-dest COMMAND x360n_canary_resolve_dest_checks)
set_tests_properties(sdkcheck.canary-resolve-dest PROPERTIES LABELS project TIMEOUT 30)

# patch 0191: Canary negative host viewport origin.
add_executable(x360n_canary_viewport_checks sdkcheck/canary_viewport_probe.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/draw_state.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/../../xenos/register_file.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/../../xenos/register.cpp
    ${X360_REXGLUE_DIR}/src/gpu/xenos/xenos_settings.h)
target_link_libraries(x360n_canary_viewport_checks PRIVATE ${X360N_SDK_LIBRARIES})
target_include_directories(x360n_canary_viewport_checks PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
x360n_stage_runtime_dlls(x360n_canary_viewport_checks)
add_test(NAME sdkcheck.canary-viewport COMMAND x360n_canary_viewport_checks)
set_tests_properties(sdkcheck.canary-viewport PROPERTIES LABELS project TIMEOUT 30)

# 0246: preserve CPU-owned bytes surrounding partial shader memory exports.
# The same raster/query fixture also exercises the real Xenos plugin explicitly.
add_executable(x360n_xenos_occlusion_probe EXCLUDE_FROM_ALL native/occlusion_probe.cpp)
target_compile_definitions(x360n_xenos_occlusion_probe PRIVATE X360N_QUERY_XENOS=1)
target_link_libraries(x360n_xenos_occlusion_probe PRIVATE ${X360N_SDK_LIBRARIES})
x360n_stage_runtime_dlls(x360n_xenos_occlusion_probe PLUGIN_TARGETS rexgpu-xenos)

# The CPU contract is portable; actual GPU probes are explicit opt-in targets.
add_executable(x360n_memexport_readback_tests sdkcheck/memexport_readback_test.cpp)
target_link_libraries(x360n_memexport_readback_tests PRIVATE ${X360N_SDK_LIBRARIES} Catch2::Catch2WithMain)
x360n_stage_runtime_dlls(x360n_memexport_readback_tests)
add_test(NAME sdkcheck.memexport-readback-contract COMMAND x360n_memexport_readback_tests)
set_tests_properties(sdkcheck.memexport-readback-contract PROPERTIES LABELS project TIMEOUT 30)
add_executable(x360n_memexport_readback_probe EXCLUDE_FROM_ALL sdkcheck/memexport_readback_probe.cpp)
target_include_directories(x360n_memexport_readback_probe PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
target_include_directories(x360n_memexport_readback_probe PRIVATE ${X360_REXGLUE_DIR}/src)
target_compile_definitions(x360n_memexport_readback_probe PRIVATE X360N_PROBE_HAS_VULKAN=$<BOOL:${REXGLUE_USE_VULKAN}>)
if(REXGLUE_USE_VULKAN)
  target_link_libraries(x360n_memexport_readback_probe PRIVATE Vulkan::Headers)
endif()
target_link_libraries(x360n_memexport_readback_probe PRIVATE ${X360N_SDK_LIBRARIES})
target_compile_definitions(x360n_memexport_readback_probe PRIVATE REX_GPU_XENOS_STATIC)
target_link_libraries(x360n_memexport_readback_probe PRIVATE rexgpu-xenos-base)
x360n_stage_runtime_dlls(x360n_memexport_readback_probe PLUGIN_TARGETS rexgpu-xenos)

# Native asynchronous pipeline creation: worker pool and deferred command
# order. Platform-neutral (no GPU, no D3D12/Vulkan), built on every host.
add_executable(x360n_native_pipeline_tests native/pipeline_recording_test.cpp
    ${CMAKE_SOURCE_DIR}/src/native/pipeline/pipeline_workers.cpp
    native/capture_format_test.cpp ${CMAKE_SOURCE_DIR}/src/native/capture/capture_format.cpp)
target_link_libraries(x360n_native_pipeline_tests PRIVATE Catch2::Catch2WithMain xxHash::xxhash)
x360n_stage_runtime_dlls(x360n_native_pipeline_tests)
add_test(NAME native.pipeline-recording COMMAND x360n_native_pipeline_tests [native-pipeline])
set_tests_properties(native.pipeline-recording PROPERTIES LABELS project TIMEOUT 60)
add_test(NAME native.capture-format COMMAND x360n_native_pipeline_tests [native-capture])
set_tests_properties(native.capture-format PROPERTIES LABELS project TIMEOUT 60)
if(WIN32)
  # Redundant D3D12 state filtering against a fake list (d3d12.h types only).
  target_sources(x360n_native_pipeline_tests PRIVATE native/d3d12_commands_test.cpp)
  add_test(NAME native.d3d12-commands COMMAND x360n_native_pipeline_tests [native-d3d12])
  set_tests_properties(native.d3d12-commands PROPERTIES LABELS project TIMEOUT 60)
endif()

foreach(_graphics_check x360n_resolve_checks x360n_fixed16_resolve_checks
    x360n_inactive_fetch_checks x360n_canary_texture_layout_checks
    x360n_canary_format_alias_checks x360n_canary_texture_bounds_checks
    x360n_canary_resolve_dest_checks x360n_canary_viewport_checks
    x360n_memexport_readback_tests)
  target_include_directories(${_graphics_check} PRIVATE ${X360_REXGLUE_DIR}/src)
  target_link_libraries(${_graphics_check} PRIVATE rexgpu-xenos-base xxHash::xxhash)
  target_compile_definitions(${_graphics_check} PRIVATE REX_GPU_XENOS_STATIC)
endforeach()
foreach(_catch_check x360n_inactive_fetch_checks x360n_canary_texture_layout_checks
    x360n_canary_viewport_checks)
  target_link_libraries(${_catch_check} PRIVATE Catch2::Catch2WithMain)
endforeach()
