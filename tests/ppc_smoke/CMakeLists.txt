# Our own PowerPC program through the SDK's official test pipeline:
#   ppc_add_test_binary (SDK cmake/ppc_test_pipeline.cmake: as -> ld -> .bin + nm .map)
#   -> rexglue recompile-tests -> generated C++ -> clang++ -> Catch2 executable.
# Mirrors external/rexglue-sdk/tests/ppc/CMakeLists.txt.
set(X360N_PPC_OBJ_DIR ${CMAKE_CURRENT_BINARY_DIR}/obj)
set(X360N_PPC_BIN_DIR ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(X360N_PPC_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${X360N_PPC_OBJ_DIR} ${X360N_PPC_BIN_DIR} ${X360N_PPC_GEN_DIR})

file(GLOB X360N_PPC_ASM ${CMAKE_CURRENT_SOURCE_DIR}/asm/*.s)
set(X360N_PPC_OUTPUTS "")
block(SCOPE_FOR VARIABLES PROPAGATE X360N_PPC_OUTPUTS)
    # The module locates tools/binutils via PROJECT_SOURCE_DIR.
    set(PROJECT_SOURCE_DIR "${X360_REXGLUE_DIR}")
    include(${X360_REXGLUE_DIR}/cmake/ppc_test_pipeline.cmake)
    foreach(asm ${X360N_PPC_ASM})
        ppc_add_test_binary(${asm} ${X360N_PPC_OBJ_DIR} ${X360N_PPC_BIN_DIR})
        get_filename_component(stem ${asm} NAME_WE)
        list(APPEND X360N_PPC_OUTPUTS ${X360N_PPC_BIN_DIR}/${stem}.bin ${X360N_PPC_BIN_DIR}/${stem}.map)
    endforeach()
endblock()

add_custom_command(
    OUTPUT ${X360N_PPC_GEN_DIR}/ppc_test_functions.cpp
           ${X360N_PPC_GEN_DIR}/ppc_test_cases.cpp
           ${X360N_PPC_GEN_DIR}/ppc_test_decls.h
    COMMAND $<TARGET_FILE:rexglue> recompile-tests
            --output=${X360N_PPC_GEN_DIR}
            --bin-dir=${X360N_PPC_BIN_DIR}
            --asm-dir=${CMAKE_CURRENT_SOURCE_DIR}/asm
    DEPENDS ${X360N_PPC_OUTPUTS} ${X360N_PPC_ASM} rexglue
    COMMENT "x360n: recompiling own PPC smoke program with rexglue recompile-tests"
    VERBATIM)

add_executable(x360n_ppc_smoke
    ${X360N_PPC_GEN_DIR}/ppc_test_functions.cpp
    ${X360N_PPC_GEN_DIR}/ppc_test_cases.cpp)
target_include_directories(x360n_ppc_smoke PRIVATE ${X360_REXGLUE_DIR}/include ${X360N_PPC_GEN_DIR})
target_link_libraries(x360n_ppc_smoke PRIVATE simde rexruntime fmt::fmt Catch2::Catch2WithMain)
# Same effective flags as the SDK's ppc_tests: its directory-wide options
# (SDK CMakeLists.txt add_compile_options) plus its target options.
target_compile_options(x360n_ppc_smoke PRIVATE
    -fno-strict-aliasing -ffp-model=strict -fno-char8_t
    -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING
    -UREXGLUE_ENABLE_PERF_COUNTERS -UREXGLUE_PROFILE_GUEST_FUNCTIONS
    -msse4.1 -mssse3)
add_custom_command(TARGET x360n_ppc_smoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:x360n_ppc_smoke> $<TARGET_FILE_DIR:x360n_ppc_smoke>
    COMMAND_EXPAND_LISTS)
catch_discover_tests(x360n_ppc_smoke TEST_PREFIX "x360n.ppc." PROPERTIES LABELS "project" TIMEOUT 60
    DISCOVERY_MODE PRE_TEST)
