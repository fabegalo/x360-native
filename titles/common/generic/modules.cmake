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
    set(_module_name "${target}")
    if(WIN32)
        # Ninja's ANSI file IO cannot reliably record deps/mtimes once the full
        # object path exceeds MAX_PATH. Keep content IDs in the public DLL name,
        # but use a compact, deterministic private target directory. A collision
        # fails configuration rather than sharing another module's products.
        string(SHA256 _module_identity "${target}")
        string(SUBSTRING "${_module_identity}" 0 20 _module_key)
        set(target "x360n_aot_${_module_key}")
        if(TARGET ${target} OR TARGET ${_module_name})
            message(FATAL_ERROR "Duplicate or colliding AOT module target: ${_module_name}")
        endif()
    endif()
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
    if(WIN32)
        set_target_properties(${target} PROPERTIES
            OUTPUT_NAME "${_module_name}" PDB_NAME "${_module_name}"
            DEFINE_SYMBOL "${_module_name}_EXPORTS"
            X360N_AOT_ORIGINAL_TARGET "${_module_name}")
        # Preserve the existing explicit build target as well as the DLL name.
        add_custom_target(${_module_name} DEPENDS ${target})
    endif()
endfunction()
