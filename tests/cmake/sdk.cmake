# Included from tests/CMakeLists.txt; paths and target outputs stay rooted there.
if(WIN32)
  add_executable(x360n_display_overlay_shader_check tools/display_overlay_shader_check.cpp)
  target_link_libraries(x360n_display_overlay_shader_check PRIVATE d3dcompiler)
  add_test(NAME sdkcheck.display-overlay-shader COMMAND x360n_display_overlay_shader_check
    ${PROJECT_SOURCE_DIR}/external/rexglue-sdk/src/graphics/shaders/display_overlay.hlsl)
  set_tests_properties(sdkcheck.display-overlay-shader PROPERTIES LABELS project TIMEOUT 20)
  add_executable(x360n_display_overlay_gpu_check EXCLUDE_FROM_ALL tools/display_overlay_gpu_check.cpp)
  target_sources(x360n_display_overlay_gpu_check PRIVATE ../external/rexglue-sdk/src/graphics/register_file.cpp)
  target_link_libraries(x360n_display_overlay_gpu_check PRIVATE rexruntime d3d12 d3dcompiler dxgi)
  x360n_stage_runtime_dlls(x360n_display_overlay_gpu_check)
  # Explicit offscreen GPU diagnostic, not automatically added to CTest.
endif()
if(WIN32)
  add_executable(x360n_crt_fiber_probe EXCLUDE_FROM_ALL tools/crt_fiber_probe.cpp)
  target_link_libraries(x360n_crt_fiber_probe PRIVATE rexruntime)
  target_compile_options(x360n_crt_fiber_probe PRIVATE
      -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
  x360n_stage_runtime_dlls(x360n_crt_fiber_probe)
  add_executable(x360n_profile_settings_probe EXCLUDE_FROM_ALL tools/profile_settings_probe.cpp)
  target_link_libraries(x360n_profile_settings_probe PRIVATE rexruntime x360n_core)
  target_compile_options(x360n_profile_settings_probe PRIVATE
      -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
  x360n_stage_runtime_dlls(x360n_profile_settings_probe)
  add_executable(x360n_profile_17559_probe EXCLUDE_FROM_ALL tools/profile_17559_probe.cpp
      ../titles/common/dashboard_assets.cpp)
  target_link_libraries(x360n_profile_17559_probe PRIVATE rexruntime x360n_core)
  target_compile_options(x360n_profile_17559_probe PRIVATE
      -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
  x360n_stage_runtime_dlls(x360n_profile_17559_probe)
  add_executable(x360n_storage_17559_probe EXCLUDE_FROM_ALL tools/storage_17559_probe.cpp
      ../titles/common/dashboard_assets.cpp)
  target_link_libraries(x360n_storage_17559_probe PRIVATE rexruntime x360n_core)
  target_compile_options(x360n_storage_17559_probe PRIVATE
      -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
  x360n_stage_runtime_dlls(x360n_storage_17559_probe)
  add_executable(x360n_notification_17559_probe EXCLUDE_FROM_ALL tools/notification_17559_probe.cpp
      ../titles/common/dashboard_assets.cpp)
  target_link_libraries(x360n_notification_17559_probe PRIVATE rexruntime x360n_core)
  target_compile_options(x360n_notification_17559_probe PRIVATE
      -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
  x360n_stage_runtime_dlls(x360n_notification_17559_probe)
endif()
# SDK behavior checks (link the SDK codegen library).
add_executable(x360n_sdk_checks sdkcheck/depfile_escape_test.cpp)
target_sources(x360n_sdk_checks PRIVATE sdkcheck/gpu_backend_test.cpp)
target_compile_definitions(x360n_sdk_checks PRIVATE X360N_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
target_sources(x360n_sdk_checks PRIVATE sdkcheck/sha1_test.cpp)
if(WIN32)
  target_link_libraries(x360n_sdk_checks PRIVATE bcrypt)
endif()
target_sources(x360n_sdk_checks PRIVATE sdkcheck/gapfill_boundary_test.cpp sdkcheck/synthetic_xex.cpp)
target_sources(x360n_sdk_checks PRIVATE sdkcheck/virtual_thunk_discovery_test.cpp)
target_sources(x360n_sdk_checks PRIVATE sdkcheck/crt_fiber_discovery_test.cpp)
target_sources(x360n_sdk_checks PRIVATE sdkcheck/ppc_md_decode_test.cpp)
target_sources(x360n_sdk_checks PRIVATE sdkcheck/ctr_dispatch_test.cpp)
target_link_libraries(x360n_sdk_checks PRIVATE rexcodegen Catch2::Catch2WithMain)
target_compile_options(x360n_sdk_checks PRIVATE
    -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
# Runtime DLLs staged on every build, not only when this executable relinks.
x360n_stage_runtime_dlls(x360n_sdk_checks)
catch_discover_tests(x360n_sdk_checks TEST_PREFIX "sdkcheck." PROPERTIES LABELS "project" TIMEOUT 60
    DISCOVERY_MODE PRE_TEST)

# Explicit native integration check: generate a synthetic XEX with the production
# pipeline, compile its real output, and execute it against the real dispatcher.
# Kept out of ALL; no proprietary content and no game/visual session.
set(ctr_fixture "${CMAKE_CURRENT_BINARY_DIR}/ctr_fixture")
set(ctr_generated "${ctr_fixture}/x360n_codegen/byte_ctr/out")
add_custom_command(
    OUTPUT "${ctr_generated}/byte_ctr_recomp.0.cpp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ctr_fixture}"
    COMMAND ${CMAKE_COMMAND} -E env "TEMP=${ctr_fixture}" "TMP=${ctr_fixture}"
        X360N_KEEP_TEST_DIRS=1 $<TARGET_FILE:x360n_sdk_checks>
        "Byte computed dispatch preserves CTR without guessing table length"
    DEPENDS x360n_sdk_checks
    VERBATIM)
add_executable(x360n_ctr_dispatch_native EXCLUDE_FROM_ALL
    sdkcheck/ctr_dispatch_native.cpp "${ctr_generated}/byte_ctr_recomp.0.cpp")
target_include_directories(x360n_ctr_dispatch_native PRIVATE "${ctr_generated}")
target_compile_definitions(x360n_ctr_dispatch_native PRIVATE
    REX_HOST_FUNCTION_TABLE=1 X360N_CTR_FIXTURE="${ctr_fixture}/x360n_codegen/byte_ctr")
target_link_libraries(x360n_ctr_dispatch_native PRIVATE rexruntime simde x360n_ppc_options)
target_compile_options(x360n_ctr_dispatch_native PRIVATE -msse4.1
    -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
x360n_stage_runtime_dlls(x360n_ctr_dispatch_native)

set(bounded_fixture "${CMAKE_CURRENT_BINARY_DIR}/bounded_fixture")
set(bounded_generated "${bounded_fixture}/x360n_codegen/bounded_byte/out")
add_custom_command(
    OUTPUT "${bounded_generated}/bounded_byte_recomp.0.cpp" "${bounded_generated}/bounded_byte_init.cpp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${bounded_fixture}"
    COMMAND ${CMAKE_COMMAND} -E env "TEMP=${bounded_fixture}" "TMP=${bounded_fixture}"
        X360N_KEEP_TEST_DIRS=1 $<TARGET_FILE:x360n_sdk_checks>
        "Guarded byte dispatch leaves the following callback callable"
    DEPENDS x360n_sdk_checks
    VERBATIM)
add_executable(x360n_bounded_byte_native EXCLUDE_FROM_ALL
    sdkcheck/bounded_byte_native.cpp "${bounded_generated}/bounded_byte_recomp.0.cpp"
    "${bounded_generated}/bounded_byte_init.cpp")
target_include_directories(x360n_bounded_byte_native PRIVATE "${bounded_generated}")
target_compile_definitions(x360n_bounded_byte_native PRIVATE REX_HOST_FUNCTION_TABLE=1
    X360N_BOUNDED_FIXTURE="${bounded_fixture}/x360n_codegen/bounded_byte")
target_link_libraries(x360n_bounded_byte_native PRIVATE rexruntime simde x360n_ppc_options)
target_compile_options(x360n_bounded_byte_native PRIVATE -msse4.1
    -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
x360n_stage_runtime_dlls(x360n_bounded_byte_native)

add_executable(x360n_usbcam_unavailable_checks sdkcheck/usbcam_unavailable_probe.cpp)
target_link_libraries(x360n_usbcam_unavailable_checks PRIVATE rexruntime)
x360n_stage_runtime_dlls(x360n_usbcam_unavailable_checks)
if(WIN32)
  add_test(NAME sdkcheck.usbcam-unavailable COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File ${CMAKE_CURRENT_SOURCE_DIR}/sdkcheck/usbcam_unavailable_test.ps1
    -Executable $<TARGET_FILE:x360n_usbcam_unavailable_checks>)
  set_tests_properties(sdkcheck.usbcam-unavailable PROPERTIES LABELS project TIMEOUT 60)
endif()

# Explicit analysis-only corpus diagnostic; never starts guest code or runs in CTest.
add_executable(x360n_aot_discovery_probe EXCLUDE_FROM_ALL tools/aot_discovery_probe.cpp)
target_link_libraries(x360n_aot_discovery_probe PRIVATE rexcodegen)
x360n_stage_runtime_dlls(x360n_aot_discovery_probe)

# Explicit synthetic diagnostic, deliberately outside ALL and CTest. Exit 78
# records an unsupported image-page attribute contract; never runs a game.
add_executable(x360n_user_mode_page_probe EXCLUDE_FROM_ALL tools/user_mode_page_probe.cpp)
target_link_libraries(x360n_user_mode_page_probe PRIVATE rexruntime)
x360n_stage_runtime_dlls(x360n_user_mode_page_probe)

# Minimal ReXApp built like a title, for the teardown checks in
# sdkcheck/app_shutdown_test.cpp (patches 0002 and 0004). It runs the synthetic
# XEX that the test writes; see shutdown_probe/probe_app.cpp.
add_executable(x360n_shutdown_probe WIN32 shutdown_probe/probe_app.cpp)
target_link_libraries(x360n_shutdown_probe PRIVATE rex::runtime)
rexglue_configure_target(x360n_shutdown_probe GPU_PLUGINS xenos)
x360n_stage_runtime_dlls(x360n_shutdown_probe PLUGIN_TARGETS rexgpu-xenos)

# SDK runtime checks (link rexruntime): behavior our local SDK patches fix
# (patches/rexglue-sdk/). Synthetic inputs only.
if(WIN32)
  # Explicit opt-in: changes foreground only between its own synthetic windows.
  # No guest, renderer, physical controller or injected keyboard input.
  add_executable(x360n_session_focus_checks EXCLUDE_FROM_ALL sdkcheck/session_focus_test.cpp)
  target_link_libraries(x360n_session_focus_checks PRIVATE rexruntime x360n_core Catch2::Catch2WithMain)
  x360n_stage_runtime_dlls(x360n_session_focus_checks)
endif()

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
    sdkcheck/entrypoint_dispatch_test.cpp
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
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/fullscreen_toggle_test.cpp)
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
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/display_overlay_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE ../external/rexglue-sdk/src/graphics/register_file.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/system_ui_input_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/local_profile_gamercard_test.cpp)

target_sources(x360n_sdk_runtime_checks PRIVATE
    sdkcheck/offline_achievement_store_test.cpp
    sdkcheck/offline_achievement_api_test.cpp
    sdkcheck/achievement_ui_test.cpp
    sdkcheck/achievement_cached_title_test.cpp
    sdkcheck/achievement_xdbf_test.cpp
    sdkcheck/offline_achievement_metadata_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/physical_heap_transaction_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/xctd_query_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/mic_array_idle_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/xex_heap_ownership_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/xam_allocation_test.cpp)
if(WIN32)
  target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/xex_key_test.cpp)
  target_link_libraries(x360n_sdk_runtime_checks PRIVATE bcrypt)
endif()
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/games_played_launch_test.cpp)
target_compile_definitions(x360n_sdk_runtime_checks PRIVATE X360N_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
target_link_libraries(x360n_sdk_runtime_checks PRIVATE tomlplusplus)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/audio_drain_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/profile_ui_request_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/message_box_request_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/keyboard_request_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/keyboard_text_input_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE ../titles/dash_17559/src/system_ui.cpp)
set_source_files_properties("${PROJECT_SOURCE_DIR}/titles/dash_17559/src/system_ui.cpp"
    PROPERTIES COMPILE_DEFINITIONS X360N_SYSTEM_UI_PLUGIN=1)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/system_ui_context_test.cpp)
if(WIN32)
  add_executable(x360n_guest_fault_context_probe tools/guest_fault_context_probe.cpp
      ../titles/common/guest_fault_context.cpp)
  target_link_libraries(x360n_guest_fault_context_probe PRIVATE rexruntime)
  target_compile_options(x360n_guest_fault_context_probe PRIVATE
      -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
  x360n_stage_runtime_dlls(x360n_guest_fault_context_probe)
  target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/guest_fault_context_test.cpp
      ../titles/common/guest_fault_context.cpp)
  target_compile_definitions(x360n_sdk_runtime_checks PRIVATE
      X360N_GUEST_FAULT_PROBE="$<TARGET_FILE:x360n_guest_fault_context_probe>")
  add_dependencies(x360n_sdk_runtime_checks x360n_guest_fault_context_probe)
endif()
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/local_profile_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/profile_settings_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/profile_tile_key_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/save_storage_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/storage_device_test.cpp)
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

add_executable(x360n_firmware_exit_probe sdkcheck/firmware_exit_probe.cpp ../titles/common/launch_handoff.cpp)
add_executable(x360n_input_hotplug_probe sdkcheck/input_hotplug_probe.cpp)
target_link_libraries(x360n_input_hotplug_probe PRIVATE rexruntime)
x360n_stage_runtime_dlls(x360n_input_hotplug_probe)
add_test(NAME sdkcheck.input-hotplug-concurrency COMMAND x360n_input_hotplug_probe)
set_tests_properties(sdkcheck.input-hotplug-concurrency PROPERTIES LABELS project TIMEOUT 30)
add_executable(x360n_sdl_hotplug_probe sdkcheck/sdl_hotplug_probe.cpp
    ${PROJECT_SOURCE_DIR}/external/rexglue-sdk/src/input/sdl/sdl_input_driver.cpp)
# Driver and SDL must share one instance in this process.
target_link_libraries(x360n_sdl_hotplug_probe PRIVATE rexruntime)
target_compile_options(x360n_sdl_hotplug_probe PRIVATE
    -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
x360n_stage_runtime_dlls(x360n_sdl_hotplug_probe)
add_test(NAME sdkcheck.sdl-hotplug-events COMMAND x360n_sdl_hotplug_probe)
set_tests_properties(sdkcheck.sdl-hotplug-events PROPERTIES LABELS project TIMEOUT 30)
target_link_libraries(x360n_firmware_exit_probe PRIVATE rexruntime)
x360n_stage_runtime_dlls(x360n_firmware_exit_probe)
if(WIN32)
  add_test(NAME sdkcheck.firmware-exit COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File ${CMAKE_CURRENT_SOURCE_DIR}/sdkcheck/firmware_exit_test.ps1
    -Executable $<TARGET_FILE:x360n_firmware_exit_probe>)
  set_tests_properties(sdkcheck.firmware-exit PROPERTIES LABELS project TIMEOUT 30)
endif()
# Runtime DLLs staged on every build, not only when this executable relinks.
x360n_stage_runtime_dlls(x360n_sdk_runtime_checks)
add_executable(x360n_1888_bridge_checks
    sdkcheck/dashboard_1888_bridge_test.cpp
    ../titles/dash_1888/src/system_ui_app_load.cpp
    ../titles/dash_1888/src/legacy_ui_requests.cpp)
target_link_libraries(x360n_1888_bridge_checks PRIVATE rexruntime Catch2::Catch2WithMain)
target_compile_options(x360n_1888_bridge_checks PRIVATE
    -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
x360n_stage_runtime_dlls(x360n_1888_bridge_checks)
add_test(NAME sdkcheck.1888-native-bridges COMMAND x360n_1888_bridge_checks)
set_tests_properties(sdkcheck.1888-native-bridges PROPERTIES LABELS project TIMEOUT 30)
if(WIN32)
  add_executable(x360n_1888_allocation_probe EXCLUDE_FROM_ALL tools/aot_xam_allocation_probe.cpp)
  target_sources(x360n_1888_allocation_probe PRIVATE
      ../titles/dash_1888/src/system_ui.cpp ../titles/common/dashboard_assets.cpp)
  target_compile_definitions(x360n_1888_allocation_probe PRIVATE X360N_1888_UI_DIAGNOSTIC=1)
  target_link_libraries(x360n_1888_allocation_probe PRIVATE rexruntime x360n_core)
  target_compile_options(x360n_1888_allocation_probe PRIVATE
      -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
  x360n_stage_runtime_dlls(x360n_1888_allocation_probe)
endif()
catch_discover_tests(x360n_sdk_runtime_checks TEST_PREFIX "sdkcheck." PROPERTIES LABELS "project" TIMEOUT 120
    DISCOVERY_MODE PRE_TEST)
if(WIN32)
  add_test(NAME sdkcheck.offline-achievements-processes COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File ${CMAKE_CURRENT_SOURCE_DIR}/local/offline-achievements-process.ps1
    -Executable $<TARGET_FILE:x360n_sdk_runtime_checks>)
  set_tests_properties(sdkcheck.offline-achievements-processes PROPERTIES LABELS project TIMEOUT 50)
  add_test(NAME sdkcheck.save-storage-processes COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File ${CMAKE_CURRENT_SOURCE_DIR}/local/save-storage-process.ps1
    -Executable $<TARGET_FILE:x360n_sdk_runtime_checks>)
  set_tests_properties(sdkcheck.save-storage-processes PROPERTIES LABELS project TIMEOUT 75)
endif()
