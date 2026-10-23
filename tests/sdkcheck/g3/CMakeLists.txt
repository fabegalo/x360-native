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
