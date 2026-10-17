# Included from tests/CMakeLists.txt; paths and target outputs stay rooted there.
add_subdirectory(ppc_smoke)

# Independent synthetic LDI import contract (no proprietary input).
add_executable(x360n_ldi_checks sdkcheck/ldi_test.cpp)
target_link_libraries(x360n_ldi_checks PRIVATE rexruntime)
x360n_stage_runtime_dlls(x360n_ldi_checks)
add_test(NAME sdkcheck.LDI COMMAND x360n_ldi_checks)
set_tests_properties(sdkcheck.LDI PROPERTIES LABELS "project" TIMEOUT 30)

# Avatar request lifecycle; no runtime or proprietary assets needed.
add_executable(x360n_avatar_assets_checks avatar/assets_request_test.cpp
    ${PROJECT_SOURCE_DIR}/titles/common/avatar/avatar_assets_request.cpp)
add_test(NAME avatar.AssetsRequest COMMAND x360n_avatar_assets_checks)
add_test(NAME avatar.ModifierCounts COMMAND x360n_avatar_assets_checks --modifier-counts)
set_tests_properties(avatar.ModifierCounts PROPERTIES LABELS "project" TIMEOUT 15)
set_tests_properties(avatar.AssetsRequest PROPERTIES LABELS "project" TIMEOUT 15)

add_subdirectory(sdkcheck/vavguw)

# Explicit manual diagnostic only; never part of automatic CTest runs.
if(WIN32)
  add_executable(x360n_xenos_cycle_probe EXCLUDE_FROM_ALL local/xenos_cycle_probe.cpp)
  target_link_libraries(x360n_xenos_cycle_probe PRIVATE x360n_core)
endif()
