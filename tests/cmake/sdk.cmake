# Included from tests/CMakeLists.txt; paths and target outputs stay rooted there.
# SDK behavior checks (link the SDK codegen library).
add_executable(x360n_sdk_checks sdkcheck/depfile_escape_test.cpp)
target_sources(x360n_sdk_checks PRIVATE sdkcheck/gapfill_boundary_test.cpp sdkcheck/synthetic_xex.cpp)
target_sources(x360n_sdk_checks PRIVATE sdkcheck/virtual_thunk_discovery_test.cpp)
target_link_libraries(x360n_sdk_checks PRIVATE rexcodegen Catch2::Catch2WithMain)
target_compile_options(x360n_sdk_checks PRIVATE
    -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
# Runtime DLLs staged on every build, not only when this executable relinks.
x360n_stage_runtime_dlls(x360n_sdk_checks)
catch_discover_tests(x360n_sdk_checks TEST_PREFIX "sdkcheck." PROPERTIES LABELS "project" TIMEOUT 60
    DISCOVERY_MODE PRE_TEST)

# Explicit analysis-only corpus diagnostic; never starts guest code or runs in CTest.
add_executable(x360n_aot_discovery_probe EXCLUDE_FROM_ALL tools/aot_discovery_probe.cpp)
target_link_libraries(x360n_aot_discovery_probe PRIVATE rexcodegen)
x360n_stage_runtime_dlls(x360n_aot_discovery_probe)

# Minimal ReXApp built like a title, for the teardown checks in
# sdkcheck/app_shutdown_test.cpp (patches 0002 and 0004). It runs the synthetic
# XEX that the test writes; see shutdown_probe/probe_app.cpp.
add_executable(x360n_shutdown_probe WIN32 shutdown_probe/probe_app.cpp)
target_link_libraries(x360n_shutdown_probe PRIVATE rex::runtime)
rexglue_configure_target(x360n_shutdown_probe GPU_PLUGINS xenos)
x360n_stage_runtime_dlls(x360n_shutdown_probe PLUGIN_TARGETS rexgpu-xenos)

# SDK runtime checks (link rexruntime): behavior our local SDK patches fix
# (patches/rexglue-sdk/). Synthetic inputs only.
add_executable(x360n_perf_counter_probe EXCLUDE_FROM_ALL performance/counter_probe.cpp)
add_executable(x360n_register_info_probe EXCLUDE_FROM_ALL
    performance/register_info_probe.cpp
    "${PROJECT_SOURCE_DIR}/external/rexglue-sdk/src/graphics/register_file.cpp")
target_link_libraries(x360n_register_info_probe PRIVATE rexruntime)
x360n_stage_runtime_dlls(x360n_register_info_probe)
target_link_libraries(x360n_perf_counter_probe PRIVATE rexruntime)
x360n_stage_runtime_dlls(x360n_perf_counter_probe)
# A benchmark is an explicit command, never an automatic CTest workload.

add_executable(x360n_sdk_runtime_checks
    sdkcheck/perf_counter_test.cpp
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
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/system_command_buffer_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/system_ui_input_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/local_profile_gamercard_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/physical_heap_transaction_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/xctd_query_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/mic_array_idle_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/xex_heap_ownership_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/games_played_launch_test.cpp)
target_compile_definitions(x360n_sdk_runtime_checks PRIVATE X360N_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
target_link_libraries(x360n_sdk_runtime_checks PRIVATE tomlplusplus)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/audio_drain_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/profile_ui_request_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/system_ui_context_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/local_profile_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/save_storage_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/signin_task_test.cpp)
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
if(WIN32)
  add_test(NAME sdkcheck.save-storage-processes COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File ${CMAKE_CURRENT_SOURCE_DIR}/local/save-storage-process.ps1
    -Executable $<TARGET_FILE:x360n_sdk_runtime_checks>)
  set_tests_properties(sdkcheck.save-storage-processes PROPERTIES LABELS project TIMEOUT 75)
endif()
