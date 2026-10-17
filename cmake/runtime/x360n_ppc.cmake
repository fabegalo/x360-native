# Compile contract for recompiled PowerPC, shared by titles and PPC tests.
include_guard(GLOBAL)
add_library(x360n_ppc_options INTERFACE)
target_compile_options(x360n_ppc_options INTERFACE
    -fno-strict-aliasing -ffp-model=strict)

# The pinned SDK exports source inventories for the entrypoint and guest DLLs.
# Resolve their consumers after the host has been declared. Do not infer PPC
# from target names or propagate the contract through rexruntime to native apps.
function(x360n_configure_generated_ppc)
    set(_sources ${REXGLUE_ENTRYPOINT_GENERATED_SOURCES})
    get_cmake_property(_variables VARIABLES)
    foreach(_variable IN LISTS _variables)
        if(_variable MATCHES "^DLL_SOURCES_")
            list(APPEND _sources ${${_variable}})
        endif()
    endforeach()
    get_property(_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_target IN LISTS _targets)
        get_target_property(_target_sources ${_target} SOURCES)
        foreach(_source IN LISTS _target_sources)
            if(_source IN_LIST _sources)
                # Target usage requirements also cover cmake_pch.hxx.cxx.
                # PRIVATE keeps them off the native host linking an OBJECT lib.
                target_link_libraries(${_target} PRIVATE x360n_ppc_options)
                break()
            endif()
        endforeach()
    endforeach()
endfunction()
