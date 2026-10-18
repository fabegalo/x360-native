# Included from tests/CMakeLists.txt; paths and target outputs stay rooted there.
# Helper child process for supervisor tests (test fixture, not a title).
add_executable(x360n_test_child supervisor/test_child.cpp)
add_executable(x360n_session_child supervisor/session_child.cpp)
target_link_libraries(x360n_session_child PRIVATE x360n_core)

add_executable(x360n_tests
    supervisor/sha256_test.cpp
    supervisor/xex_identity_test.cpp
    supervisor/paths_test.cpp
    supervisor/report_test.cpp
    supervisor/process_test.cpp
    supervisor/session_test.cpp
    supervisor/renderer_settings_test.cpp
    supervisor/console_settings_test.cpp
    supervisor/session_process_test.cpp
    supervisor/preparation_test.cpp
    supervisor/games_played_test.cpp
)
target_link_libraries(x360n_tests PRIVATE x360n_core Catch2::Catch2WithMain)
target_sources(x360n_tests PRIVATE supervisor/dashboard_assets_test.cpp
    ../titles/common/dashboard_assets.cpp)
target_link_libraries(x360n_tests PRIVATE tomlplusplus)
target_compile_definitions(x360n_tests PRIVATE
    X360N_TEST_CHILD="$<TARGET_FILE:x360n_test_child>"
    X360N_SESSION_CHILD="$<TARGET_FILE:x360n_session_child>"
    X360N_EXE="$<TARGET_FILE:x360n>"
    X360N_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
add_dependencies(x360n_tests x360n x360n_test_child x360n_session_child)
# Avatar asset catalog reader shared by the titles (titles/common/avatar), synthetic data.
target_sources(x360n_tests PRIVATE avatar/avatar_toc_test.cpp avatar/avatar_metadata_test.cpp
    ${PROJECT_SOURCE_DIR}/titles/common/avatar/avatar_metadata.cpp
    ${PROJECT_SOURCE_DIR}/titles/common/avatar/avatar_toc.cpp)
# x360n-content extract-disc over synthetic GDFX images and apply-patch key file
# handling (runs the tool as a process; no key material).
target_sources(x360n_tests PRIVATE content/extract_disc_test.cpp content/apply_patch_keys_test.cpp)
target_sources(x360n_tests PRIVATE content/extraction_manifest_test.cpp
    ${PROJECT_SOURCE_DIR}/src/content/extraction_manifest.cpp)
target_compile_definitions(x360n_tests PRIVATE X360N_CONTENT_EXE="$<TARGET_FILE:x360n-content>")
add_dependencies(x360n_tests x360n-content)
catch_discover_tests(x360n_tests TEST_PREFIX "x360n." PROPERTIES LABELS "project" TIMEOUT 120)
if(WIN32)
  add_executable(x360n_aot_module_registration_probe EXCLUDE_FROM_ALL tools/aot_module_registration_probe.cpp)
  target_link_libraries(x360n_aot_module_registration_probe PRIVATE x360n_core)
  target_include_directories(x360n_aot_module_registration_probe PRIVATE
    ${X360_REXGLUE_DIR}/thirdparty/xxHash ${X360_REXGLUE_DIR}/thirdparty/simde)
  add_test(NAME x360n.benchmark-process-summary COMMAND pwsh -NoProfile -File
    ${CMAKE_CURRENT_SOURCE_DIR}/performance/process_summary_test.ps1)
  set_tests_properties(x360n.benchmark-process-summary PROPERTIES LABELS project TIMEOUT 30)
  add_test(NAME x360n.benchmark-instrumentation COMMAND pwsh -NoProfile -File
    ${CMAKE_CURRENT_SOURCE_DIR}/performance/instrumentation_test.ps1)
  set_tests_properties(x360n.benchmark-instrumentation PROPERTIES LABELS project TIMEOUT 30)
  add_test(NAME x360n.benchmark-summary COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File ${CMAKE_CURRENT_SOURCE_DIR}/performance/summary_test.ps1)
  set_tests_properties(x360n.benchmark-summary PROPERTIES LABELS project TIMEOUT 30)
  add_test(NAME x360n.benchmark-comparison COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File ${CMAKE_CURRENT_SOURCE_DIR}/performance/compare_test.ps1)
  set_tests_properties(x360n.benchmark-comparison PROPERTIES LABELS project TIMEOUT 30)
  add_test(NAME x360n.benchmark-export COMMAND pwsh -NoProfile -File
    ${CMAKE_CURRENT_SOURCE_DIR}/performance/export_test.ps1)
  set_tests_properties(x360n.benchmark-export PROPERTIES LABELS project TIMEOUT 30)
  add_test(NAME x360n.benchmark-reduction COMMAND pwsh -NoProfile -File
    ${CMAKE_CURRENT_SOURCE_DIR}/performance/reduce_test.ps1)
  set_tests_properties(x360n.benchmark-reduction PROPERTIES LABELS project TIMEOUT 30)
  add_test(NAME x360n.dashboard-assets-build-gate COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File ${CMAKE_CURRENT_SOURCE_DIR}/build_isolation/dashboard_assets.ps1)
  set_tests_properties(x360n.dashboard-assets-build-gate PROPERTIES LABELS project TIMEOUT 30)
  add_test(NAME x360n.build-leases COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File ${CMAKE_CURRENT_SOURCE_DIR}/build_isolation/leases_test.ps1)
  set_tests_properties(x360n.build-leases PROPERTIES LABELS project TIMEOUT 30)
  add_test(NAME x360n.generic-aot-build-lease COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File ${CMAKE_CURRENT_SOURCE_DIR}/build_isolation/generic_aot.ps1)
  set_tests_properties(x360n.generic-aot-build-lease PROPERTIES LABELS project TIMEOUT 30)
  add_test(NAME x360n.preparation-receipt-publication COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File ${CMAKE_CURRENT_SOURCE_DIR}/supervisor/preparation_receipt_test.ps1)
  set_tests_properties(x360n.preparation-receipt-publication PROPERTIES LABELS project TIMEOUT 60)
  add_test(NAME x360n.ppc.compile-contract COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File ${CMAKE_CURRENT_SOURCE_DIR}/ppc_contract/contract_test.ps1)
  set_tests_properties(x360n.ppc.compile-contract PROPERTIES LABELS project TIMEOUT 180)
endif()
