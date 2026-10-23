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
