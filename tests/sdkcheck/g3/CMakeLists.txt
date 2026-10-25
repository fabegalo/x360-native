# Real PM4/shared-memory services, without a GPU, window or game.
add_executable(x360n_register_memory_checks ../register_memory_coherence_probe.cpp
    ${X360_REXGLUE_DIR}/src/graphics/command_processor.cpp
    ${X360_REXGLUE_DIR}/src/graphics/graphics_system.cpp
    ${X360_REXGLUE_DIR}/src/graphics/shared_memory.cpp
    ${X360_REXGLUE_DIR}/src/graphics/register_file.cpp
    ${X360_REXGLUE_DIR}/src/graphics/registers.cpp
    ${X360_REXGLUE_DIR}/src/graphics/flags.cpp)
target_link_libraries(x360n_register_memory_checks PRIVATE rexruntime)
target_include_directories(x360n_register_memory_checks PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
x360n_stage_runtime_dlls(x360n_register_memory_checks)
add_test(NAME sdkcheck.register-memory-coherence COMMAND x360n_register_memory_checks)
set_tests_properties(sdkcheck.register-memory-coherence PROPERTIES LABELS project TIMEOUT 30)

add_executable(x360n_resolve_readback_identity_checks ../resolve_readback_identity_probe.cpp
    ${X360_REXGLUE_DIR}/src/graphics/register_file.cpp)
target_link_libraries(x360n_resolve_readback_identity_checks PRIVATE rexruntime)
x360n_stage_runtime_dlls(x360n_resolve_readback_identity_checks)
add_test(NAME sdkcheck.resolve-readback-identity COMMAND x360n_resolve_readback_identity_checks)
set_tests_properties(sdkcheck.resolve-readback-identity PROPERTIES LABELS project TIMEOUT 30)

add_executable(x360n_vulkan_upload_ordering_checks ../vulkan_upload_ordering_probe.cpp)
target_include_directories(x360n_vulkan_upload_ordering_checks PRIVATE ${X360_REXGLUE_DIR}/include)
add_test(NAME sdkcheck.Vulkan-upload-ordering COMMAND x360n_vulkan_upload_ordering_checks)
set_tests_properties(sdkcheck.Vulkan-upload-ordering PROPERTIES LABELS project TIMEOUT 30)

add_executable(x360n_frame_timing_checks frame_timing.cpp)
target_link_libraries(x360n_frame_timing_checks PRIVATE rexruntime)
x360n_stage_runtime_dlls(x360n_frame_timing_checks)
add_test(NAME sdkcheck.Native-FrameTiming COMMAND x360n_frame_timing_checks)
set_tests_properties(sdkcheck.Native-FrameTiming PROPERTIES LABELS project TIMEOUT 10)

if(REXGLUE_USE_VULKAN)
  if(PYTHON_EXECUTABLE)
    set(_upload_probe_python ${PYTHON_EXECUTABLE})
  else()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_upload_probe_python ${Python3_EXECUTABLE})
  endif()
  set(_upload_probe_inc ${CMAKE_CURRENT_BINARY_DIR}/vulkan_repeat_upload_backend.inc)
  add_custom_command(OUTPUT ${_upload_probe_inc}
      COMMAND ${_upload_probe_python} ${CMAKE_CURRENT_SOURCE_DIR}/../generate_vulkan_upload_probe.py
          ${X360_REXGLUE_DIR} ${_upload_probe_inc}
      DEPENDS ../generate_vulkan_upload_probe.py
          ${X360_REXGLUE_DIR}/src/graphics/vulkan/texture_cache.cpp
          ${X360_REXGLUE_DIR}/src/graphics/vulkan/command_processor.cpp
          ${X360_REXGLUE_DIR}/src/graphics/vulkan/deferred_command_buffer.cpp
      VERBATIM)
  add_executable(x360n_vulkan_repeat_upload_checks ../vulkan_repeat_upload_probe.cpp ${_upload_probe_inc})
  target_include_directories(x360n_vulkan_repeat_upload_checks PRIVATE ${CMAKE_CURRENT_BINARY_DIR}
      ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
  target_link_libraries(x360n_vulkan_repeat_upload_checks PRIVATE rexruntime Vulkan::Headers)
  x360n_stage_runtime_dlls(x360n_vulkan_repeat_upload_checks)
  add_test(NAME sdkcheck.Vulkan-repeat-upload COMMAND x360n_vulkan_repeat_upload_checks)
  set_tests_properties(sdkcheck.Vulkan-repeat-upload PROPERTIES LABELS project TIMEOUT 30)

  set(_spirv_gpu ${X360_REXGLUE_DIR}/src/graphics)
  add_executable(x360n_spirv_interlock_checks spirv_interlock.cpp spirv_rectangle.cpp
      ${_spirv_gpu}/util/draw.cpp ${_spirv_gpu}/registers.cpp
      ${_spirv_gpu}/flags.cpp ${_spirv_gpu}/format/ucode.cpp
      ${_spirv_gpu}/pipeline/shader/shader.cpp
      ${_spirv_gpu}/pipeline/shader/translator.cpp
      ${_spirv_gpu}/pipeline/shader/translator_disasm.cpp
      ${_spirv_gpu}/pipeline/shader/spirv_builder.cpp
      ${_spirv_gpu}/pipeline/shader/spirv.cpp
      ${_spirv_gpu}/pipeline/shader/spirv_translator.cpp
      ${_spirv_gpu}/pipeline/shader/spirv_translator_alu.cpp
      ${_spirv_gpu}/pipeline/shader/spirv_translator_fetch.cpp
      ${_spirv_gpu}/pipeline/shader/spirv_translator_memexport.cpp
      ${_spirv_gpu}/pipeline/shader/spirv_translator_rb.cpp)
  target_link_libraries(x360n_spirv_interlock_checks PRIVATE rexruntime xxHash::xxhash
      Vulkan::Headers glslang::SPIRV glslang MachineIndependent GenericCodeGen OSDependent OGLCompiler)
  target_include_directories(x360n_spirv_interlock_checks PRIVATE
      ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
  x360n_stage_runtime_dlls(x360n_spirv_interlock_checks)
  add_test(NAME sdkcheck.SPIRV-interlock-control-flow COMMAND x360n_spirv_interlock_checks)
  set_tests_properties(sdkcheck.SPIRV-interlock-control-flow PROPERTIES LABELS project TIMEOUT 30)
  add_test(NAME sdkcheck.SPIRV-rectangle-replay-phi COMMAND x360n_spirv_interlock_checks)
  set_tests_properties(sdkcheck.SPIRV-rectangle-replay-phi PROPERTIES LABELS project TIMEOUT 30)
endif()

# patch 0185: EDRAM fixed16 s5.10 pack/unpack in ROV (DXBC) and FSI (SPIR-V).
if(REXGLUE_USE_VULKAN AND WIN32)
  set(_fixed16_gpu ${X360_REXGLUE_DIR}/src/graphics)
  add_executable(x360n_edram_fixed16_checks edram_fixed16.cpp
      ${_fixed16_gpu}/util/draw.cpp ${_fixed16_gpu}/register_file.cpp ${_fixed16_gpu}/registers.cpp
      ${_fixed16_gpu}/flags.cpp ${_fixed16_gpu}/format/ucode.cpp
      ${_fixed16_gpu}/pipeline/render_target/cache.cpp ${_fixed16_gpu}/util/draw_extent_estimator.cpp ${_fixed16_gpu}/pipeline/shader/interpreter.cpp
      ${_fixed16_gpu}/pipeline/shader/shader.cpp
      ${_fixed16_gpu}/pipeline/shader/translator.cpp
      ${_fixed16_gpu}/pipeline/shader/translator_disasm.cpp
      ${_fixed16_gpu}/pipeline/shader/dxbc.cpp
      ${_fixed16_gpu}/pipeline/shader/dxbc_translator.cpp
      ${_fixed16_gpu}/pipeline/shader/dxbc_translator_alu.cpp
      ${_fixed16_gpu}/pipeline/shader/dxbc_translator_fetch.cpp
      ${_fixed16_gpu}/pipeline/shader/dxbc_translator_memexport.cpp
      ${_fixed16_gpu}/pipeline/shader/dxbc_translator_om.cpp
      ${_fixed16_gpu}/pipeline/shader/spirv_builder.cpp
      ${_fixed16_gpu}/pipeline/shader/spirv.cpp
      ${_fixed16_gpu}/pipeline/shader/spirv_translator.cpp
      ${_fixed16_gpu}/pipeline/shader/spirv_translator_alu.cpp
      ${_fixed16_gpu}/pipeline/shader/spirv_translator_fetch.cpp
      ${_fixed16_gpu}/pipeline/shader/spirv_translator_memexport.cpp
      ${_fixed16_gpu}/pipeline/shader/spirv_translator_rb.cpp
      ${X360_REXGLUE_DIR}/thirdparty/dxbc/DXBCChecksum.cpp)
  target_link_libraries(x360n_edram_fixed16_checks PRIVATE rexruntime xxHash::xxhash
      Vulkan::Headers glslang::SPIRV glslang MachineIndependent GenericCodeGen OSDependent OGLCompiler)
  target_include_directories(x360n_edram_fixed16_checks PRIVATE
      ${X360_REXGLUE_DIR} ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
  x360n_stage_runtime_dlls(x360n_edram_fixed16_checks)
  add_test(NAME sdkcheck.canary-edram-fixed16 COMMAND x360n_edram_fixed16_checks)
  set_tests_properties(sdkcheck.canary-edram-fixed16 PROPERTIES LABELS project TIMEOUT 30)
endif()

# patch 0188: Canary SPIR-V stacked texture and 2x-as-4x sample mask fixes.
if(REXGLUE_USE_VULKAN)
  set(_canary_gpu ${X360_REXGLUE_DIR}/src/graphics)
  add_executable(x360n_spirv_canary_checks spirv_canary.cpp
      ${_canary_gpu}/util/draw.cpp ${_canary_gpu}/registers.cpp
      ${_canary_gpu}/flags.cpp ${_canary_gpu}/format/ucode.cpp
      ${_canary_gpu}/pipeline/shader/shader.cpp
      ${_canary_gpu}/pipeline/shader/translator.cpp
      ${_canary_gpu}/pipeline/shader/translator_disasm.cpp
      ${_canary_gpu}/pipeline/shader/spirv_builder.cpp
      ${_canary_gpu}/pipeline/shader/spirv.cpp
      ${_canary_gpu}/pipeline/shader/spirv_translator.cpp
      ${_canary_gpu}/pipeline/shader/spirv_translator_alu.cpp
      ${_canary_gpu}/pipeline/shader/spirv_translator_fetch.cpp
      ${_canary_gpu}/pipeline/shader/spirv_translator_memexport.cpp
      ${_canary_gpu}/pipeline/shader/spirv_translator_rb.cpp)
  target_link_libraries(x360n_spirv_canary_checks PRIVATE rexruntime xxHash::xxhash
      Vulkan::Headers glslang::SPIRV glslang MachineIndependent GenericCodeGen OSDependent OGLCompiler)
  target_include_directories(x360n_spirv_canary_checks PRIVATE
      ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
  x360n_stage_runtime_dlls(x360n_spirv_canary_checks)
  add_test(NAME sdkcheck.canary-spirv COMMAND x360n_spirv_canary_checks)
  set_tests_properties(sdkcheck.canary-spirv PROPERTIES LABELS project TIMEOUT 30)
endif()

# patch 0189: Canary DXBC stacked texture layer clamp.
if(WIN32)
  set(_dxbc_gpu ${X360_REXGLUE_DIR}/src/graphics)
  add_executable(x360n_dxbc_canary_checks dxbc_canary.cpp
      ${_dxbc_gpu}/util/draw.cpp ${_dxbc_gpu}/register_file.cpp ${_dxbc_gpu}/registers.cpp
      ${_dxbc_gpu}/flags.cpp ${_dxbc_gpu}/format/ucode.cpp
      ${_dxbc_gpu}/pipeline/render_target/cache.cpp ${_dxbc_gpu}/util/draw_extent_estimator.cpp
      ${_dxbc_gpu}/pipeline/shader/interpreter.cpp
      ${_dxbc_gpu}/pipeline/shader/shader.cpp
      ${_dxbc_gpu}/pipeline/shader/translator.cpp
      ${_dxbc_gpu}/pipeline/shader/translator_disasm.cpp
      ${_dxbc_gpu}/pipeline/shader/dxbc.cpp
      ${_dxbc_gpu}/pipeline/shader/dxbc_translator.cpp
      ${_dxbc_gpu}/pipeline/shader/dxbc_translator_alu.cpp
      ${_dxbc_gpu}/pipeline/shader/dxbc_translator_fetch.cpp
      ${_dxbc_gpu}/pipeline/shader/dxbc_translator_memexport.cpp
      ${_dxbc_gpu}/pipeline/shader/dxbc_translator_om.cpp
      ${X360_REXGLUE_DIR}/thirdparty/dxbc/DXBCChecksum.cpp)
  target_link_libraries(x360n_dxbc_canary_checks PRIVATE rexruntime xxHash::xxhash)
  target_include_directories(x360n_dxbc_canary_checks PRIVATE
      ${X360_REXGLUE_DIR} ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
  x360n_stage_runtime_dlls(x360n_dxbc_canary_checks)
  add_test(NAME sdkcheck.canary-dxbc COMMAND x360n_dxbc_canary_checks)
  set_tests_properties(sdkcheck.canary-dxbc PROPERTIES LABELS project TIMEOUT 30)
endif()

# patch 0190: Canary PM4_WAIT_IB_PFD_COMPLETE handling.
add_executable(x360n_canary_pm4_checks ../canary_pm4_probe.cpp
    ${X360_REXGLUE_DIR}/src/graphics/command_processor.cpp
    ${X360_REXGLUE_DIR}/src/graphics/graphics_system.cpp
    ${X360_REXGLUE_DIR}/src/graphics/shared_memory.cpp
    ${X360_REXGLUE_DIR}/src/graphics/register_file.cpp
    ${X360_REXGLUE_DIR}/src/graphics/registers.cpp
    ${X360_REXGLUE_DIR}/src/graphics/flags.cpp)
target_link_libraries(x360n_canary_pm4_checks PRIVATE rexruntime)
target_include_directories(x360n_canary_pm4_checks PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
x360n_stage_runtime_dlls(x360n_canary_pm4_checks)
add_test(NAME sdkcheck.canary-pm4 COMMAND x360n_canary_pm4_checks)
set_tests_properties(sdkcheck.canary-pm4 PROPERTIES LABELS project TIMEOUT 30)

# patch 0195: Canary ROV/FSI depth and color alias bits.
set(_alias_gpu ${X360_REXGLUE_DIR}/src/graphics)
add_executable(x360n_edram_alias_checks edram_alias.cpp
    ${_alias_gpu}/util/draw.cpp ${_alias_gpu}/register_file.cpp ${_alias_gpu}/registers.cpp
    ${_alias_gpu}/flags.cpp ${_alias_gpu}/format/ucode.cpp
    ${_alias_gpu}/pipeline/render_target/cache.cpp ${_alias_gpu}/util/draw_extent_estimator.cpp
    ${_alias_gpu}/pipeline/shader/interpreter.cpp
    ${_alias_gpu}/pipeline/shader/shader.cpp
    ${_alias_gpu}/pipeline/shader/translator.cpp
    ${_alias_gpu}/pipeline/shader/translator_disasm.cpp)
target_link_libraries(x360n_edram_alias_checks PRIVATE rexruntime xxHash::xxhash)
target_include_directories(x360n_edram_alias_checks PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
x360n_stage_runtime_dlls(x360n_edram_alias_checks)
add_test(NAME sdkcheck.canary-edram-alias COMMAND x360n_edram_alias_checks)
set_tests_properties(sdkcheck.canary-edram-alias PROPERTIES LABELS project TIMEOUT 30)

# 0199: compare the actual shader's scalar functions with an independent oracle.
set(_gamma_math ${CMAKE_CURRENT_BINARY_DIR}/gamma_resolve_math.h)
if(PYTHON_EXECUTABLE)
  set(_gamma_probe_python ${PYTHON_EXECUTABLE})
else()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(_gamma_probe_python ${Python3_EXECUTABLE})
endif()
add_custom_command(OUTPUT ${_gamma_math}
    COMMAND ${_gamma_probe_python} ${CMAKE_CURRENT_SOURCE_DIR}/../generate_gamma_math_probe.py
      ${X360_REXGLUE_DIR}/src/graphics/shaders/resolve_sources/resolve.xesli ${_gamma_math}
    DEPENDS ../generate_gamma_math_probe.py
      ${X360_REXGLUE_DIR}/src/graphics/shaders/resolve_sources/resolve.xesli
      ${X360_REXGLUE_DIR}/src/graphics/shaders/resolve_sources/pixel_formats.xesli
    VERBATIM)
add_executable(x360n_gamma_resolve_checks ../gamma_resolve_cpu.cpp ${_gamma_math}
    ${X360_REXGLUE_DIR}/src/graphics/util/draw.cpp
    ${X360_REXGLUE_DIR}/src/graphics/register_file.cpp
    ${X360_REXGLUE_DIR}/src/graphics/registers.cpp
    ${X360_REXGLUE_DIR}/src/graphics/flags.cpp)
target_include_directories(x360n_gamma_resolve_checks PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR} ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
target_link_libraries(x360n_gamma_resolve_checks PRIVATE rexruntime)
x360n_stage_runtime_dlls(x360n_gamma_resolve_checks)
add_test(NAME sdkcheck.canary-gamma-resolve COMMAND x360n_gamma_resolve_checks)
set_tests_properties(sdkcheck.canary-gamma-resolve PROPERTIES LABELS project TIMEOUT 30)
add_test(NAME sdkcheck.fixed16-full-resources
    COMMAND ${_gamma_probe_python} ${CMAKE_CURRENT_SOURCE_DIR}/../fixed16_full_resources.py)
set_tests_properties(sdkcheck.fixed16-full-resources PROPERTIES LABELS project TIMEOUT 30)
if(REXGLUE_USE_VULKAN AND WIN32)
  add_executable(x360n_gamma_resolve_gpu_checks ../gamma_resolve_gpu.cpp)
  target_include_directories(x360n_gamma_resolve_gpu_checks PRIVATE
      ${X360_REXGLUE_DIR} ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
  target_link_libraries(x360n_gamma_resolve_gpu_checks PRIVATE rexruntime Vulkan::Headers d3d12 dxgi)
  x360n_stage_runtime_dlls(x360n_gamma_resolve_gpu_checks)
  # Opt-in headless GPU test; each fence is bounded to five seconds.
endif()
# Maintenance tool only; normal Release builds consume the shipped resources.
add_executable(x360n_resolve_dxbc_checksum EXCLUDE_FROM_ALL
    ../../../scripts/shaders/resolve_dxbc_checksum.cpp
    ${X360_REXGLUE_DIR}/thirdparty/dxbc/DXBCChecksum.cpp)
target_include_directories(x360n_resolve_dxbc_checksum PRIVATE
    ${X360_REXGLUE_DIR}/thirdparty/dxbc)
if(REXGLUE_USE_VULKAN)
  add_executable(x360n_resolve_shader_compiler EXCLUDE_FROM_ALL
      ../../../scripts/shaders/compile_resolve_spirv.cpp
      ${X360_REXGLUE_DIR}/thirdparty/glslang/StandAlone/ResourceLimits.cpp)
  target_include_directories(x360n_resolve_shader_compiler PRIVATE
      ${X360_REXGLUE_DIR}/thirdparty/glslang)
  target_link_libraries(x360n_resolve_shader_compiler PRIVATE
      glslang::SPIRV glslang MachineIndependent GenericCodeGen OSDependent OGLCompiler)
endif()
# 0198: execute common renderer ownership and coordinate transforms.
add_executable(x360n_edram_window_checks edram_window_offsets.cpp
    ${_alias_gpu}/util/draw.cpp ${_alias_gpu}/register_file.cpp ${_alias_gpu}/registers.cpp
    ${_alias_gpu}/flags.cpp ${_alias_gpu}/format/ucode.cpp
    ${_alias_gpu}/pipeline/render_target/cache.cpp ${_alias_gpu}/util/draw_extent_estimator.cpp
    ${_alias_gpu}/pipeline/shader/interpreter.cpp ${_alias_gpu}/pipeline/shader/shader.cpp
    ${_alias_gpu}/pipeline/shader/translator.cpp ${_alias_gpu}/pipeline/shader/translator_disasm.cpp)
target_link_libraries(x360n_edram_window_checks PRIVATE rexruntime xxHash::xxhash)
target_include_directories(x360n_edram_window_checks PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
x360n_stage_runtime_dlls(x360n_edram_window_checks)
add_test(NAME sdkcheck.canary-edram-window COMMAND x360n_edram_window_checks)
set_tests_properties(sdkcheck.canary-edram-window PROPERTIES LABELS project TIMEOUT 30)

# 0197: shared Xenos sample positions and viewport sample-fraction scaling.
add_executable(x360n_canary_msaa_checks ../canary_msaa_probe.cpp
    ${X360_REXGLUE_DIR}/src/graphics/util/draw.cpp
    ${X360_REXGLUE_DIR}/src/graphics/register_file.cpp
    ${X360_REXGLUE_DIR}/src/graphics/registers.cpp
    ${X360_REXGLUE_DIR}/src/graphics/flags.cpp)
target_link_libraries(x360n_canary_msaa_checks PRIVATE rexruntime)
target_include_directories(x360n_canary_msaa_checks PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
x360n_stage_runtime_dlls(x360n_canary_msaa_checks)
add_test(NAME sdkcheck.canary-msaa COMMAND x360n_canary_msaa_checks)
set_tests_properties(sdkcheck.canary-msaa PROPERTIES LABELS project TIMEOUT 30)

if(REXGLUE_USE_VULKAN AND WIN32)
  add_executable(x360n_msaa_gpu_checks ../msaa_gpu_probe.cpp
      ${X360_REXGLUE_DIR}/src/graphics/util/draw.cpp
      ${X360_REXGLUE_DIR}/src/graphics/register_file.cpp
      ${X360_REXGLUE_DIR}/src/graphics/registers.cpp
      ${X360_REXGLUE_DIR}/src/graphics/flags.cpp)
  target_include_directories(x360n_msaa_gpu_checks PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
  target_link_libraries(x360n_msaa_gpu_checks PRIVATE rexruntime Vulkan::Headers d3d12 dxgi)
  x360n_stage_runtime_dlls(x360n_msaa_gpu_checks)
  # Opt-in: run manually when a bounded synthetic GPU test is authorized.
  add_executable(x360n_edram_window_gpu_checks ../edram_window_gpu.cpp
      ${X360_REXGLUE_DIR}/src/graphics/util/draw.cpp
      ${X360_REXGLUE_DIR}/src/graphics/register_file.cpp
      ${X360_REXGLUE_DIR}/src/graphics/registers.cpp
      ${X360_REXGLUE_DIR}/src/graphics/flags.cpp
      ${X360_REXGLUE_DIR}/thirdparty/glslang/StandAlone/ResourceLimits.cpp)
  target_include_directories(x360n_edram_window_gpu_checks PRIVATE
      ${X360_REXGLUE_DIR}/thirdparty/renderdoc ${X360_REXGLUE_DIR}/thirdparty/glslang)
  target_link_libraries(x360n_edram_window_gpu_checks PRIVATE rexruntime Vulkan::Headers d3d12 dxgi
      glslang::SPIRV glslang MachineIndependent GenericCodeGen OSDependent OGLCompiler)
  x360n_stage_runtime_dlls(x360n_edram_window_gpu_checks)
  # Manual opt-in, each GPU fence has a five-second timeout.
endif()
