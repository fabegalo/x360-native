# Included from tests/CMakeLists.txt; paths and target outputs stay rooted there.
add_executable(x360n_resolve_checks sdkcheck/resolve_empty_probe.cpp
    ${X360_REXGLUE_DIR}/src/graphics/util/draw.cpp
    ${X360_REXGLUE_DIR}/src/graphics/register_file.cpp
    ${X360_REXGLUE_DIR}/src/graphics/registers.cpp
    ${X360_REXGLUE_DIR}/src/graphics/flags.cpp)
target_link_libraries(x360n_resolve_checks PRIVATE rexruntime)
target_include_directories(x360n_resolve_checks PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
x360n_stage_runtime_dlls(x360n_resolve_checks)
add_test(NAME sdkcheck.empty-resolve COMMAND x360n_resolve_checks)
set_tests_properties(sdkcheck.empty-resolve PROPERTIES LABELS project TIMEOUT 30)

# patch 0087: s5.10 fixed-point render targets and raw signed-integer resolves.
add_executable(x360n_fixed16_resolve_checks sdkcheck/fixed16_resolve_probe.cpp
    ${X360_REXGLUE_DIR}/src/graphics/util/draw.cpp
    ${X360_REXGLUE_DIR}/src/graphics/register_file.cpp
    ${X360_REXGLUE_DIR}/src/graphics/registers.cpp
    ${X360_REXGLUE_DIR}/src/graphics/flags.cpp)
target_link_libraries(x360n_fixed16_resolve_checks PRIVATE rexruntime)
target_include_directories(x360n_fixed16_resolve_checks PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
x360n_stage_runtime_dlls(x360n_fixed16_resolve_checks)
add_test(NAME sdkcheck.fixed16-resolve COMMAND x360n_fixed16_resolve_checks)
set_tests_properties(sdkcheck.fixed16-resolve PROPERTIES LABELS project TIMEOUT 30)

# Conservative invalid-descriptor proof, synthetic microcode only.
add_executable(x360n_inactive_fetch_checks sdkcheck/inactive_vertex_fetch_probe.cpp
    ${X360_REXGLUE_DIR}/src/graphics/register_file.cpp
    ${X360_REXGLUE_DIR}/src/graphics/registers.cpp
    ${X360_REXGLUE_DIR}/src/graphics/flags.cpp
    ${X360_REXGLUE_DIR}/src/graphics/format/ucode.cpp
    ${X360_REXGLUE_DIR}/src/graphics/pipeline/shader/shader.cpp
    ${X360_REXGLUE_DIR}/src/graphics/pipeline/shader/translator.cpp
    ${X360_REXGLUE_DIR}/src/graphics/pipeline/shader/translator_disasm.cpp)
target_link_libraries(x360n_inactive_fetch_checks PRIVATE rexruntime)
target_include_directories(x360n_inactive_fetch_checks PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
x360n_stage_runtime_dlls(x360n_inactive_fetch_checks)
add_test(NAME sdkcheck.inactive-vertex-fetch COMMAND x360n_inactive_fetch_checks)
set_tests_properties(sdkcheck.inactive-vertex-fetch PROPERTIES LABELS project TIMEOUT 30)

# A separate staging producer keeps the CPU-only checks independent of GPU
# plugins staged by other tests in the parent directory.
add_subdirectory(sdkcheck/g3)

# patch 0183: Canary guest texture addressing (packed tails, base map pages).
add_executable(x360n_canary_texture_layout_checks sdkcheck/canary_texture_layout_probe.cpp
    ${X360_REXGLUE_DIR}/src/graphics/pipeline/texture/util.cpp
    ${X360_REXGLUE_DIR}/src/graphics/pipeline/texture/info.cpp
    ${X360_REXGLUE_DIR}/src/graphics/pipeline/texture/info_formats.cpp)
target_link_libraries(x360n_canary_texture_layout_checks PRIVATE rexruntime)
x360n_stage_runtime_dlls(x360n_canary_texture_layout_checks)
add_test(NAME sdkcheck.canary-texture-layout COMMAND x360n_canary_texture_layout_checks)
set_tests_properties(sdkcheck.canary-texture-layout PROPERTIES LABELS project TIMEOUT 30)

# patch 0184: Canary 8-bit texture / resolve format aliases.
add_executable(x360n_canary_format_alias_checks sdkcheck/canary_format_alias_probe.cpp
    ${X360_REXGLUE_DIR}/src/graphics/util/draw.cpp
    ${X360_REXGLUE_DIR}/src/graphics/register_file.cpp
    ${X360_REXGLUE_DIR}/src/graphics/registers.cpp
    ${X360_REXGLUE_DIR}/src/graphics/flags.cpp)
target_link_libraries(x360n_canary_format_alias_checks PRIVATE rexruntime)
target_include_directories(x360n_canary_format_alias_checks PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
x360n_stage_runtime_dlls(x360n_canary_format_alias_checks)
add_test(NAME sdkcheck.canary-format-alias COMMAND x360n_canary_format_alias_checks)
set_tests_properties(sdkcheck.canary-format-alias PROPERTIES LABELS project TIMEOUT 30)

# patch 0186: Canary guest texture layout and exact tiled 3D bounds.
add_executable(x360n_canary_texture_bounds_checks sdkcheck/canary_texture_bounds_probe.cpp
    ${X360_REXGLUE_DIR}/src/graphics/pipeline/texture/util.cpp
    ${X360_REXGLUE_DIR}/src/graphics/pipeline/texture/info.cpp
    ${X360_REXGLUE_DIR}/src/graphics/pipeline/texture/info_formats.cpp)
target_link_libraries(x360n_canary_texture_bounds_checks PRIVATE rexruntime)
x360n_stage_runtime_dlls(x360n_canary_texture_bounds_checks)
add_test(NAME sdkcheck.canary-texture-bounds COMMAND x360n_canary_texture_bounds_checks)
set_tests_properties(sdkcheck.canary-texture-bounds PROPERTIES LABELS project TIMEOUT 60)

# patch 0187: Canary tiled resolve destination base phase below 32bpp.
add_executable(x360n_canary_resolve_dest_checks sdkcheck/canary_resolve_dest_probe.cpp
    ${X360_REXGLUE_DIR}/src/graphics/util/draw.cpp
    ${X360_REXGLUE_DIR}/src/graphics/register_file.cpp
    ${X360_REXGLUE_DIR}/src/graphics/registers.cpp
    ${X360_REXGLUE_DIR}/src/graphics/flags.cpp)
target_link_libraries(x360n_canary_resolve_dest_checks PRIVATE rexruntime)
target_include_directories(x360n_canary_resolve_dest_checks PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
x360n_stage_runtime_dlls(x360n_canary_resolve_dest_checks)
add_test(NAME sdkcheck.canary-resolve-dest COMMAND x360n_canary_resolve_dest_checks)
set_tests_properties(sdkcheck.canary-resolve-dest PROPERTIES LABELS project TIMEOUT 30)

# patch 0191: Canary negative host viewport origin.
add_executable(x360n_canary_viewport_checks sdkcheck/canary_viewport_probe.cpp
    ${X360_REXGLUE_DIR}/src/graphics/util/draw.cpp
    ${X360_REXGLUE_DIR}/src/graphics/register_file.cpp
    ${X360_REXGLUE_DIR}/src/graphics/registers.cpp
    ${X360_REXGLUE_DIR}/src/graphics/flags.cpp)
target_link_libraries(x360n_canary_viewport_checks PRIVATE rexruntime)
target_include_directories(x360n_canary_viewport_checks PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
x360n_stage_runtime_dlls(x360n_canary_viewport_checks)
add_test(NAME sdkcheck.canary-viewport COMMAND x360n_canary_viewport_checks)
set_tests_properties(sdkcheck.canary-viewport PROPERTIES LABELS project TIMEOUT 30)

# 0246: preserve CPU-owned bytes surrounding partial shader memory exports.
# The CPU contract is portable; actual GPU probes are explicit opt-in targets.
add_executable(x360n_memexport_readback_tests sdkcheck/memexport_readback_test.cpp)
target_link_libraries(x360n_memexport_readback_tests PRIVATE rexruntime Catch2::Catch2WithMain)
x360n_stage_runtime_dlls(x360n_memexport_readback_tests)
add_test(NAME sdkcheck.memexport-readback-contract COMMAND x360n_memexport_readback_tests)
set_tests_properties(sdkcheck.memexport-readback-contract PROPERTIES LABELS project TIMEOUT 30)
add_executable(x360n_memexport_readback_probe EXCLUDE_FROM_ALL sdkcheck/memexport_readback_probe.cpp)
target_include_directories(x360n_memexport_readback_probe PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
target_link_libraries(x360n_memexport_readback_probe PRIVATE rexruntime)
x360n_stage_runtime_dlls(x360n_memexport_readback_probe PLUGIN_TARGETS rexgpu-xenos)

# Native asynchronous pipeline creation: worker pool and deferred command
# order. Platform-neutral (no GPU, no D3D12/Vulkan), built on every host.
add_executable(x360n_native_pipeline_tests native/pipeline_recording_test.cpp
    ${CMAKE_SOURCE_DIR}/src/native/pipeline/pipeline_workers.cpp)
target_link_libraries(x360n_native_pipeline_tests PRIVATE Catch2::Catch2WithMain)
x360n_stage_runtime_dlls(x360n_native_pipeline_tests)
add_test(NAME native.pipeline-recording COMMAND x360n_native_pipeline_tests [native-pipeline])
set_tests_properties(native.pipeline-recording PROPERTIES LABELS project TIMEOUT 60)
