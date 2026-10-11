# x360n-content: STFS package info/extraction on top of the SDK's
# StfsContainerDevice (compiled into rexruntime). Separate from x360n so the
# supervisor keeps not linking the runtime (docs/architecture.md, D6).
add_executable(x360n-content main.cpp)
# disasm: the SDK's binutils PowerPC decoder (thirdparty/disasm), as used by the codegen.
target_link_libraries(x360n-content PRIVATE rexruntime x360n_core disasm)
target_compile_options(x360n-content PRIVATE
    -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
# Runtime DLLs staged on every build, not only when this executable relinks.
x360n_stage_runtime_dlls(x360n-content)
