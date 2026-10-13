# Test groups (ctest name prefixes):
#   x360n.*      supervisor unit/process tests; no proprietary content
#   x360n.ppc.*  our own PowerPC program through the SDK recompiler
#   sdkcheck.*   checks of SDK behavior we depend on or diagnosed
#   env.*        depends on this machine's toolchain (doctor)
#   local.*      needs user-supplied files in .local/inputs; SKIPPED when absent
# All of the above carry the single label "project" (ctest -L project); filter a
# group by name (ctest -R "local[.]"; devshell.cmd eats "^"). One label only: catch_discover_tests
# flattens a multi-value LABELS list into PROPERTIES, which shifts the
# key/value pairs and silently drops TIMEOUT and SKIP_RETURN_CODE.
# The SDK's own suites keep their prefixes: unit.* and ppc.*.
list(APPEND CMAKE_MODULE_PATH "${X360_REXGLUE_DIR}/thirdparty/catch2/extras")
include(Catch)

# Helper child process for supervisor tests (test fixture, not a title).
add_executable(x360n_test_child supervisor/test_child.cpp)

add_executable(x360n_tests
    supervisor/sha256_test.cpp
    supervisor/xex_identity_test.cpp
    supervisor/paths_test.cpp
    supervisor/report_test.cpp
    supervisor/process_test.cpp
    supervisor/session_test.cpp
)
target_link_libraries(x360n_tests PRIVATE x360n_core Catch2::Catch2WithMain)
target_compile_definitions(x360n_tests PRIVATE
    X360N_TEST_CHILD="$<TARGET_FILE:x360n_test_child>"
    X360N_EXE="$<TARGET_FILE:x360n>"
    X360N_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
add_dependencies(x360n_tests x360n x360n_test_child)
# Avatar asset catalog reader shared by the titles (titles/common/avatar), synthetic data.
target_sources(x360n_tests PRIVATE avatar/avatar_toc_test.cpp avatar/avatar_metadata_test.cpp
    ${PROJECT_SOURCE_DIR}/titles/common/avatar/avatar_metadata.cpp
    ${PROJECT_SOURCE_DIR}/titles/common/avatar/avatar_toc.cpp)
# x360n-content extract-disc over synthetic GDFX images and apply-patch key file
# handling (runs the tool as a process; no key material).
target_sources(x360n_tests PRIVATE content/extract_disc_test.cpp content/apply_patch_keys_test.cpp)
target_compile_definitions(x360n_tests PRIVATE X360N_CONTENT_EXE="$<TARGET_FILE:x360n-content>")
add_dependencies(x360n_tests x360n-content)
catch_discover_tests(x360n_tests TEST_PREFIX "x360n." PROPERTIES LABELS "project" TIMEOUT 120)

# Integration tests over user-supplied content in .local/inputs.
add_executable(x360n_local_tests local/inputs_test.cpp local/avatar_pack_test.cpp
    ${PROJECT_SOURCE_DIR}/titles/common/avatar/avatar_metadata.cpp
    ${PROJECT_SOURCE_DIR}/titles/common/avatar/avatar_toc.cpp)
target_link_libraries(x360n_local_tests PRIVATE x360n_core Catch2::Catch2WithMain)
target_compile_definitions(x360n_local_tests PRIVATE X360N_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
catch_discover_tests(x360n_local_tests TEST_PREFIX "local." PROPERTIES LABELS "project" TIMEOUT 600)

# SDK behavior checks (link the SDK codegen library).
add_executable(x360n_sdk_checks sdkcheck/depfile_escape_test.cpp)
target_link_libraries(x360n_sdk_checks PRIVATE rexcodegen Catch2::Catch2WithMain)
target_compile_options(x360n_sdk_checks PRIVATE
    -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
# Runtime DLLs staged on every build, not only when this executable relinks.
x360n_stage_runtime_dlls(x360n_sdk_checks)
catch_discover_tests(x360n_sdk_checks TEST_PREFIX "sdkcheck." PROPERTIES LABELS "project" TIMEOUT 60
    DISCOVERY_MODE PRE_TEST)

# Minimal ReXApp built like a title, for the teardown checks in
# sdkcheck/app_shutdown_test.cpp (patches 0002 and 0004). It runs the synthetic
# XEX that the test writes; see shutdown_probe/probe_app.cpp.
add_executable(x360n_shutdown_probe WIN32 shutdown_probe/probe_app.cpp)
target_link_libraries(x360n_shutdown_probe PRIVATE rex::runtime)
rexglue_configure_target(x360n_shutdown_probe GPU_PLUGINS xenos)
x360n_stage_runtime_dlls(x360n_shutdown_probe PLUGIN_TARGETS rexgpu-xenos)

# SDK runtime checks (link rexruntime): behavior our local SDK patches fix
# (patches/rexglue-sdk/). Synthetic inputs only.
add_executable(x360n_sdk_runtime_checks
    sdkcheck/synthetic_xex.cpp
    sdkcheck/function_table_test.cpp
    sdkcheck/setup_failure_test.cpp
    sdkcheck/import_records_test.cpp
    sdkcheck/app_shutdown_test.cpp
    sdkcheck/image_page_entry_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/dashboard_region_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/xam_language_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/dpc_dispatch_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/mnk_keystroke_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/xam_datetime_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/dashboard_typeface_test.cpp
    ../titles/dash_17559/src/typeface.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/xam_common_string_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/xam_user_preference_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/storage_notification_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/aggregate_content_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/title_return_test.cpp
    ../titles/common/launch_handoff.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/persist_display_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/audio_drain_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/profile_ui_request_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/dashboard_uri_test.cpp
    ../titles/dash_17559/src/launch_uri.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE
    sdkcheck/dashboard_camera_test.cpp
    ${PROJECT_SOURCE_DIR}/titles/common/camera_state.cpp
    ${PROJECT_SOURCE_DIR}/titles/dash_17559/src/unresolved_imports.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/dashboard_6717_curl_bind_test.cpp
    ${PROJECT_SOURCE_DIR}/titles/dash_6717/src/curl_bind.cpp)
# Exercise the SDL driver and its SDL instance in the same binary; a second SDL
# instance in the test cannot configure/pause devices owned inside rexruntime.
target_link_libraries(x360n_sdk_runtime_checks PRIVATE rexruntime rexcodegen rexaudio x360n_core Catch2::Catch2WithMain)
target_compile_options(x360n_sdk_runtime_checks PRIVATE
    -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
target_compile_definitions(x360n_sdk_runtime_checks PRIVATE
    X360N_SHUTDOWN_PROBE="$<TARGET_FILE:x360n_shutdown_probe>")
add_dependencies(x360n_sdk_runtime_checks x360n_shutdown_probe)
# Runtime DLLs staged on every build, not only when this executable relinks.
x360n_stage_runtime_dlls(x360n_sdk_runtime_checks)
catch_discover_tests(x360n_sdk_runtime_checks TEST_PREFIX "sdkcheck." PROPERTIES LABELS "project" TIMEOUT 120
    DISCOVERY_MODE PRE_TEST)

add_test(NAME env.doctor COMMAND x360n doctor --config $<CONFIG>)
set_tests_properties(env.doctor PROPERTIES LABELS "project" TIMEOUT 300)

# Functional reference (docs/validation-iruka.md): runs the recompiled demo
# title built from titles/iruka and checks levels L1-L5. Needs the title build
# (cmake --preset x360n-win-amd64 in titles/iruka) and the demo assets in
# .local/inputs; skipped (77) otherwise. Opens a window, so it runs alone.
add_test(NAME local.iruka.regression
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${PROJECT_SOURCE_DIR}/scripts/regress-iruka.ps1
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(local.iruka.regression PROPERTIES
    LABELS "project" TIMEOUT 180 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)

add_subdirectory(ppc_smoke)

# Independent synthetic LDI import contract (no proprietary input).
add_executable(x360n_ldi_checks sdkcheck/ldi_test.cpp)
target_link_libraries(x360n_ldi_checks PRIVATE rexruntime)
x360n_stage_runtime_dlls(x360n_ldi_checks)
add_test(NAME sdkcheck.LDI COMMAND x360n_ldi_checks)
set_tests_properties(sdkcheck.LDI PROPERTIES LABELS "project" TIMEOUT 30)
