# Compact generic module outputs. The SDK emits each module's sources and the
# host's module_registry.cpp from the same manifest; do not synthesize PPC code.
function(x360n_add_aot_module target output)
    if(NOT EXISTS "${output}/sources.cmake")
        return() # first configure precedes codegen; build-title reconfigures
    endif()
    include("${output}/sources.cmake")
    # Export the inventory for the shared PPC compile-contract collector, which
    # applies strict aliasing/FP options to the module and its PCH after setup.
    set(DLL_SOURCES_${target} ${GENERATED_SOURCES} PARENT_SCOPE)
    add_library(${target} SHARED ${GENERATED_SOURCES})
    target_include_directories(${target} PRIVATE "${output}")
    target_link_libraries(${target} PRIVATE rex::runtime)
    if(X360N_AOT_HOST_DISPATCH)
        target_compile_definitions(${target} PRIVATE REX_HOST_FUNCTION_TABLE=1)
    endif()
    rexglue_apply_recomp_settings(${target} "${output}/@TITLE@_pch.h")
    add_dependencies(${target} @TITLE@_codegen)
    set_target_properties(${target} PROPERTIES CXX_VISIBILITY_PRESET hidden)
    rexglue_configure_module_target(${target} HOST @TITLE@)
endfunction()
