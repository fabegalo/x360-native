# Included from tests/CMakeLists.txt; paths and target outputs stay rooted there.
add_test(NAME sdkcheck.sdk-version-source COMMAND ${CMAKE_COMMAND}
  -DSDK_DIR=${PROJECT_SOURCE_DIR}/external/rexglue-sdk
  -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/sdk-version-source
  -P ${CMAKE_CURRENT_SOURCE_DIR}/sdkcheck/sdk_version_source_test.cmake)
set_tests_properties(sdkcheck.sdk-version-source PROPERTIES LABELS project TIMEOUT 60)
add_test(NAME sdkcheck.sdk-linker COMMAND ${CMAKE_COMMAND}
  -DROOT=${PROJECT_SOURCE_DIR} -DWORK=${CMAKE_CURRENT_BINARY_DIR}/sdk-linker
  -P ${CMAKE_CURRENT_SOURCE_DIR}/sdkcheck/sdk_linker_test.cmake)
set_tests_properties(sdkcheck.sdk-linker PROPERTIES LABELS project TIMEOUT 10)
add_test(NAME sdkcheck.patch-line-endings COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/sdkcheck/patch_line_endings_test.py)
set_tests_properties(sdkcheck.patch-line-endings PROPERTIES LABELS project TIMEOUT 10)
if(WIN32)
  add_executable(x360n_display_overlay_shader_check tools/display_overlay_shader_check.cpp)
  target_link_libraries(x360n_display_overlay_shader_check PRIVATE d3dcompiler)
  add_test(NAME sdkcheck.display-overlay-shader COMMAND x360n_display_overlay_shader_check
    ${PROJECT_SOURCE_DIR}/external/rexglue-sdk/src/gpu/xenos/shaders/display_overlay.hlsl)
  set_tests_properties(sdkcheck.display-overlay-shader PROPERTIES LABELS project TIMEOUT 20)
  add_executable(x360n_display_overlay_gpu_check EXCLUDE_FROM_ALL tools/display_overlay_gpu_check.cpp)
  target_sources(x360n_display_overlay_gpu_check PRIVATE ../external/rexglue-sdk/src/gpu/xenos/../../xenos/register_file.cpp)
  target_link_libraries(x360n_display_overlay_gpu_check PRIVATE ${X360N_SDK_LIBRARIES} d3d12 d3dcompiler dxgi)
  target_include_directories(x360n_display_overlay_gpu_check PRIVATE ${X360_REXGLUE_DIR}/src)
x360n_stage_runtime_dlls(x360n_display_overlay_gpu_check)
  # Explicit offscreen GPU diagnostic, not automatically added to CTest.
endif()
if(WIN32)
  add_executable(x360n_crt_fiber_probe EXCLUDE_FROM_ALL tools/crt_fiber_probe.cpp)
  target_link_libraries(x360n_crt_fiber_probe PRIVATE ${X360N_SDK_LIBRARIES})
target_link_libraries(x360n_crt_fiber_probe PRIVATE rexcodegen)
target_include_directories(x360n_crt_fiber_probe PRIVATE ${X360_REXGLUE_DIR}/src)
  target_compile_options(x360n_crt_fiber_probe PRIVATE
      -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
  x360n_stage_runtime_dlls(x360n_crt_fiber_probe)
  add_executable(x360n_profile_settings_probe EXCLUDE_FROM_ALL tools/profile_settings_probe.cpp)
  target_link_libraries(x360n_profile_settings_probe PRIVATE ${X360N_SDK_LIBRARIES} x360n_core)
  target_link_libraries(x360n_profile_settings_probe PRIVATE rexcodegen)
  target_include_directories(x360n_profile_settings_probe PRIVATE ${X360_REXGLUE_DIR}/src)
  target_compile_options(x360n_profile_settings_probe PRIVATE
      -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
  x360n_stage_runtime_dlls(x360n_profile_settings_probe)
  add_executable(x360n_profile_17559_probe EXCLUDE_FROM_ALL tools/profile_17559_probe.cpp
      ../titles/common/dashboard_assets.cpp)
  target_link_libraries(x360n_profile_17559_probe PRIVATE ${X360N_SDK_LIBRARIES} x360n_core)
  target_link_libraries(x360n_profile_17559_probe PRIVATE rexcodegen)
  target_include_directories(x360n_profile_17559_probe PRIVATE ${X360_REXGLUE_DIR}/src)
  target_compile_options(x360n_profile_17559_probe PRIVATE
      -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
  x360n_stage_runtime_dlls(x360n_profile_17559_probe)
  add_executable(x360n_storage_17559_probe EXCLUDE_FROM_ALL tools/storage_17559_probe.cpp
      ../titles/common/dashboard_assets.cpp)
  target_link_libraries(x360n_storage_17559_probe PRIVATE ${X360N_SDK_LIBRARIES} x360n_core)
  target_link_libraries(x360n_storage_17559_probe PRIVATE rexcodegen)
  target_include_directories(x360n_storage_17559_probe PRIVATE ${X360_REXGLUE_DIR}/src)
  target_compile_options(x360n_storage_17559_probe PRIVATE
      -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
  x360n_stage_runtime_dlls(x360n_storage_17559_probe)
  add_executable(x360n_notification_17559_probe EXCLUDE_FROM_ALL tools/notification_17559_probe.cpp
      ../titles/common/dashboard_assets.cpp)
  target_link_libraries(x360n_notification_17559_probe PRIVATE ${X360N_SDK_LIBRARIES} x360n_core)
  target_link_libraries(x360n_notification_17559_probe PRIVATE rexcodegen)
  target_include_directories(x360n_notification_17559_probe PRIVATE ${X360_REXGLUE_DIR}/src)
  target_compile_options(x360n_notification_17559_probe PRIVATE
      -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
  x360n_stage_runtime_dlls(x360n_notification_17559_probe)
  add_executable(x360n_png_17559_probe EXCLUDE_FROM_ALL tools/png_17559_probe.cpp
      ../titles/common/dashboard_assets.cpp ../titles/common/guest_fault_context.cpp
      ../titles/dash_17559/src/system_ui_images.cpp)
  target_link_libraries(x360n_png_17559_probe PRIVATE ${X360N_SDK_LIBRARIES} x360n_core)
  target_compile_definitions(x360n_png_17559_probe PRIVATE
      __imp__XamPngEncode=x360n_test_XamPngEncode
      __imp__XamPngEncodeEx=x360n_test_XamPngEncodeEx)
  target_link_libraries(x360n_png_17559_probe PRIVATE rexcodegen)
  target_include_directories(x360n_png_17559_probe PRIVATE ${X360_REXGLUE_DIR}/src)
  target_compile_options(x360n_png_17559_probe PRIVATE
      -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
  x360n_stage_runtime_dlls(x360n_png_17559_probe)
endif()
# SDK behavior checks (link the SDK codegen library).
add_executable(x360n_sdk_checks sdkcheck/depfile_escape_test.cpp)
target_sources(x360n_sdk_checks PRIVATE sdkcheck/midasm_bindings_test.cpp)
target_link_libraries(x360n_sdk_checks PRIVATE tomlplusplus)
target_sources(x360n_sdk_checks PRIVATE sdkcheck/gpu_backend_test.cpp)
target_compile_definitions(x360n_sdk_checks PRIVATE X360N_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
target_sources(x360n_sdk_checks PRIVATE sdkcheck/sha1_test.cpp)
if(WIN32)
  target_link_libraries(x360n_sdk_checks PRIVATE bcrypt)
endif()
target_sources(x360n_sdk_checks PRIVATE sdkcheck/gapfill_boundary_test.cpp sdkcheck/synthetic_xex.cpp)
target_sources(x360n_sdk_checks PRIVATE sdkcheck/emission_owner_test.cpp)
target_sources(x360n_sdk_checks PRIVATE sdkcheck/virtual_thunk_discovery_test.cpp)
target_sources(x360n_sdk_checks PRIVATE sdkcheck/virtual_tail_registers_test.cpp)
target_sources(x360n_sdk_checks PRIVATE sdkcheck/descriptor_tail_test.cpp)

target_sources(x360n_sdk_checks PRIVATE sdkcheck/late_callback_discovery_test.cpp)
target_sources(x360n_sdk_checks PRIVATE sdkcheck/adjacent_referenced_entry_test.cpp)

target_sources(x360n_sdk_checks PRIVATE sdkcheck/shared_tail_entry_test.cpp)

target_sources(x360n_sdk_checks PRIVATE sdkcheck/split_default_switch_test.cpp)

target_sources(x360n_sdk_checks PRIVATE sdkcheck/msr_codegen_test.cpp)
target_sources(x360n_sdk_checks PRIVATE sdkcheck/crt_fiber_discovery_test.cpp)
target_sources(x360n_sdk_checks PRIVATE sdkcheck/ppc_md_decode_test.cpp)
target_sources(x360n_sdk_checks PRIVATE sdkcheck/ctr_dispatch_test.cpp)
target_sources(x360n_sdk_checks PRIVATE sdkcheck/junction_config_path_test.cpp)
target_link_libraries(x360n_sdk_checks PRIVATE Catch2::Catch2WithMain)
target_compile_options(x360n_sdk_checks PRIVATE
    -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
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
target_link_libraries(x360n_ctr_dispatch_native PRIVATE ${X360N_SDK_LIBRARIES} simde x360n_ppc_options)
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
target_link_libraries(x360n_bounded_byte_native PRIVATE ${X360N_SDK_LIBRARIES} simde x360n_ppc_options)
target_compile_options(x360n_bounded_byte_native PRIVATE
    -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
  target_compile_options(x360n_bounded_byte_native PRIVATE -msse4.1)
endif()
x360n_stage_runtime_dlls(x360n_bounded_byte_native)

# Small, headless execution regression for the actual generated interrupt writes.
set(msr_fixture "${CMAKE_CURRENT_BINARY_DIR}/msr_fixture")
set(msr_generated "${msr_fixture}/x360n_codegen/msr/out")
add_custom_command(
    OUTPUT "${msr_generated}/msr_recomp.0.cpp" "${msr_generated}/msr_init.cpp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${msr_fixture}"
    COMMAND ${CMAKE_COMMAND} -E env "TEMP=${msr_fixture}" "TMP=${msr_fixture}"
        X360N_KEEP_TEST_DIRS=1 $<TARGET_FILE:x360n_sdk_checks>
        "MSR interrupt writes produce a compilable native fixture"
    DEPENDS x360n_sdk_checks
    VERBATIM)
add_executable(x360n_msr_native sdkcheck/msr_native.cpp
    "${msr_generated}/msr_recomp.0.cpp")
target_include_directories(x360n_msr_native PRIVATE "${msr_generated}")
target_link_libraries(x360n_msr_native PRIVATE ${X360N_SDK_LIBRARIES} simde x360n_ppc_options)
target_compile_options(x360n_msr_native PRIVATE
    -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
  target_compile_options(x360n_msr_native PRIVATE -msse4.1)
endif()
x360n_stage_runtime_dlls(x360n_msr_native)
add_test(NAME sdkcheck.msr-native COMMAND x360n_msr_native)
set_tests_properties(sdkcheck.msr-native PROPERTIES LABELS project TIMEOUT 15)

set(large_switch_fixture "${CMAKE_CURRENT_BINARY_DIR}/large_switch_fixture")
set(large_switch_generated "${large_switch_fixture}/x360n_codegen/large_switch/out")
add_custom_command(
    OUTPUT "${large_switch_generated}/large_switch_recomp.0.cpp"
        "${large_switch_generated}/large_switch_init.cpp"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${large_switch_fixture}"
    COMMAND ${CMAKE_COMMAND} -E env "TEMP=${large_switch_fixture}" "TMP=${large_switch_fixture}"
        X360N_KEEP_TEST_DIRS=1 $<TARGET_FILE:x360n_sdk_checks>
        "Unsigned split defaults preserve complete large switch tables"
    DEPENDS x360n_sdk_checks VERBATIM)
add_executable(x360n_large_switch_native sdkcheck/split_default_switch_native.cpp
    "${large_switch_generated}/large_switch_recomp.0.cpp"
    "${large_switch_generated}/large_switch_init.cpp")
target_include_directories(x360n_large_switch_native PRIVATE "${large_switch_generated}")
target_compile_definitions(x360n_large_switch_native PRIVATE REX_HOST_FUNCTION_TABLE=1
    X360N_LARGE_SWITCH_FIXTURE="${large_switch_fixture}/x360n_codegen/large_switch")
target_link_libraries(x360n_large_switch_native PRIVATE ${X360N_SDK_LIBRARIES} simde x360n_ppc_options)
target_compile_options(x360n_large_switch_native PRIVATE
    -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64)$")
  target_compile_options(x360n_large_switch_native PRIVATE -msse4.1)
endif()
foreach(_target x360n_ctr_dispatch_native x360n_bounded_byte_native x360n_large_switch_native)
  target_include_directories(${_target} PRIVATE "${PROJECT_SOURCE_DIR}/external/rexglue-sdk/src")
  target_link_libraries(${_target} PRIVATE rexcodegen)
endforeach()
x360n_stage_runtime_dlls(x360n_large_switch_native)
add_test(NAME sdkcheck.large-switch-native COMMAND x360n_large_switch_native)
set_tests_properties(sdkcheck.large-switch-native PROPERTIES LABELS project TIMEOUT 15)

add_executable(x360n_usbcam_unavailable_checks sdkcheck/usbcam_unavailable_probe.cpp)
target_link_libraries(x360n_usbcam_unavailable_checks PRIVATE ${X360N_SDK_LIBRARIES})
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
target_link_libraries(x360n_user_mode_page_probe PRIVATE ${X360N_SDK_LIBRARIES})
target_link_libraries(x360n_user_mode_page_probe PRIVATE rexcodegen)
target_include_directories(x360n_user_mode_page_probe PRIVATE ${X360_REXGLUE_DIR}/src)
x360n_stage_runtime_dlls(x360n_user_mode_page_probe)

# Minimal ReXApp built like a title, for the teardown checks in
# sdkcheck/app_shutdown_test.cpp (patches 0002 and 0004). It runs the synthetic
# XEX that the test writes; see shutdown_probe/probe_app.cpp.
# Win32 window and focus checks: Windows only, like the tests that use them.
if(WIN32)
  add_executable(x360n_shutdown_probe WIN32 shutdown_probe/probe_app.cpp)
  target_link_libraries(x360n_shutdown_probe PRIVATE ${X360N_SDK_LIBRARIES})
  rexglue_configure_target(x360n_shutdown_probe NO_COPY)
  x360n_stage_runtime_dlls(x360n_shutdown_probe PLUGIN_TARGETS rexgpu-xenos)
endif()

# SDK runtime checks (link rexruntime): behavior our local SDK patches fix
# (patches/rexglue-sdk/). Synthetic inputs only.
if(WIN32)
  # Explicit opt-in: changes foreground only between its own synthetic windows.
  # No guest, renderer, physical controller or injected keyboard input.
  add_executable(x360n_session_focus_checks EXCLUDE_FROM_ALL sdkcheck/session_focus_test.cpp)
  target_link_libraries(x360n_session_focus_checks PRIVATE ${X360N_SDK_LIBRARIES} x360n_core Catch2::Catch2WithMain)
  x360n_stage_runtime_dlls(x360n_session_focus_checks)
endif()

add_executable(x360n_perf_counter_probe EXCLUDE_FROM_ALL performance/counter_probe.cpp)
add_executable(x360n_register_info_probe EXCLUDE_FROM_ALL
    performance/register_info_probe.cpp
    "${PROJECT_SOURCE_DIR}/external/rexglue-sdk/src/gpu/xenos/../../xenos/register_file.cpp")
target_link_libraries(x360n_register_info_probe PRIVATE ${X360N_SDK_LIBRARIES})
target_include_directories(x360n_register_info_probe PRIVATE ${X360_REXGLUE_DIR}/src)
x360n_stage_runtime_dlls(x360n_register_info_probe)
target_link_libraries(x360n_perf_counter_probe PRIVATE ${X360N_SDK_LIBRARIES})
x360n_stage_runtime_dlls(x360n_perf_counter_probe)
# A benchmark is an explicit command, never an automatic CTest workload.

add_executable(x360n_sdk_runtime_checks
    sdkcheck/entrypoint_dispatch_test.cpp
    sdkcheck/perf_counter_test.cpp
    sdkcheck/synthetic_xex.cpp
    sdkcheck/function_table_test.cpp
    sdkcheck/setup_failure_test.cpp
    sdkcheck/import_records_test.cpp
    sdkcheck/image_page_entry_test.cpp)
if(WIN32)
  target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/app_shutdown_test.cpp)
endif()
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/dashboard_region_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/xam_language_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/dpc_dispatch_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/mnk_keystroke_test.cpp)
if(WIN32)
  target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/fullscreen_toggle_test.cpp)
  target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/xam_datetime_test.cpp)
endif()
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/dashboard_typeface_test.cpp
    ../titles/dash_17559/src/typeface.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/xam_common_string_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/xam_user_preference_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/storage_notification_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/aggregate_content_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/long_content_path_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/title_return_test.cpp
    ../titles/common/launch_handoff.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/persist_display_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/system_command_buffer_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/display_overlay_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE ../external/rexglue-sdk/src/gpu/xenos/../../xenos/register_file.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/system_ui_input_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/local_profile_gamercard_test.cpp)

target_sources(x360n_sdk_runtime_checks PRIVATE
    sdkcheck/offline_achievement_store_test.cpp
    sdkcheck/offline_achievement_api_test.cpp
    sdkcheck/achievement_ui_test.cpp
    sdkcheck/achievement_cached_title_test.cpp
    sdkcheck/achievement_xdbf_test.cpp
    sdkcheck/offline_achievement_metadata_test.cpp
    sdkcheck/achievement_toast_test.cpp) # 0248
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/physical_heap_transaction_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/system_heap_layout_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/xctd_query_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/mic_array_idle_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/xex_heap_ownership_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/xam_allocation_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/debug_overlay_layout_test.cpp) # 0257
if(WIN32)
  target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/xex_key_test.cpp)
  target_link_libraries(x360n_sdk_runtime_checks PRIVATE bcrypt)
endif()
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/games_played_launch_test.cpp)
target_compile_definitions(x360n_sdk_runtime_checks PRIVATE X360N_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
target_link_libraries(x360n_sdk_runtime_checks PRIVATE tomlplusplus)
target_link_libraries(x360n_sdk_runtime_checks PRIVATE glslang::SPIRV)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/audio_drain_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/profile_ui_request_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/message_box_request_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/keyboard_request_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/keyboard_text_input_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/original_xam_service_test.cpp ../titles/dash_17559/src/original_xam_services.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/original_image_service_test.cpp ../titles/dash_17559/src/system_ui_images.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE ../titles/dash_17559/src/system_ui.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE ../titles/dash_17559/src/display_transition.cpp ../titles/common/dashboard_assets.cpp)
set_source_files_properties("${PROJECT_SOURCE_DIR}/titles/dash_17559/src/system_ui.cpp"
    PROPERTIES COMPILE_DEFINITIONS X360N_SYSTEM_UI_PLUGIN=1)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/system_ui_context_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/system_ui_transition_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/system_ui_compositor_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/system_ui_task_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/png_17559_service_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/profile_picture_write_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/default_gamer_picture_test.cpp)
if(WIN32)
  add_executable(x360n_guest_fault_context_probe tools/guest_fault_context_probe.cpp
      ../titles/common/guest_fault_context.cpp)
  target_link_libraries(x360n_guest_fault_context_probe PRIVATE ${X360N_SDK_LIBRARIES})
  target_link_libraries(x360n_guest_fault_context_probe PRIVATE rexcodegen)
  target_include_directories(x360n_guest_fault_context_probe PRIVATE "${PROJECT_SOURCE_DIR}/external/rexglue-sdk/src")
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
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/avatar_profile_manifest_test.cpp
    ../titles/common/avatar/avatar_profile_manifest.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/xekeys_rsa_signature_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/package_mount_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/avatar_minicreator_request_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/profile_tile_key_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/save_storage_test.cpp)
target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/utility_drive_test.cpp)
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
add_executable(x360n_1888_bridge_checks
    sdkcheck/dashboard_1888_bridge_test.cpp
    ../titles/dash_1888/src/system_ui_app_load.cpp
    ../titles/dash_1888/src/legacy_ui_requests.cpp)
target_link_libraries(x360n_1888_bridge_checks PRIVATE ${X360N_SDK_LIBRARIES} Catch2::Catch2WithMain)
x360n_stage_runtime_dlls(x360n_1888_bridge_checks)
add_test(NAME sdkcheck.1888-native-bridges COMMAND x360n_1888_bridge_checks)
set_tests_properties(sdkcheck.1888-native-bridges PROPERTIES LABELS project TIMEOUT 30)
# Use the official white-box fixture and canonical module objects, as SDK unit_tests does.
# Runtime DLL integration is covered separately by the session host and cross-module tests.
set(_sdk_test_root "${PROJECT_SOURCE_DIR}/external/rexglue-sdk")
foreach(_sdk_checks x360n_sdk_checks x360n_sdk_runtime_checks x360n_1888_bridge_checks
    x360n_resolve_checks x360n_fixed16_resolve_checks x360n_canary_format_alias_checks
    x360n_canary_resolve_dest_checks x360n_native_tests x360n_query_state_checks
    x360n_register_memory_checks x360n_primitive_canary_checks x360n_scalar_precision_cpu
    x360n_canary_pm4_checks)
if(NOT TARGET ${_sdk_checks})
  continue()
endif()
# The canonical module objects supply these bodies in white-box tests.
get_target_property(_test_links ${_sdk_checks} LINK_LIBRARIES)
list(REMOVE_ITEM _test_links rexapp rexkernel rexfilesystem rexxam rexui rexapu rexhid rexcodegen rexglue)
set_property(TARGET ${_sdk_checks} PROPERTY LINK_LIBRARIES "${_test_links}")
target_sources(${_sdk_checks} PRIVATE
    "${_sdk_test_root}/tests/unit/kernel/kernel_fixture.cpp"
    "${_sdk_test_root}/tests/unit/test_memory.cpp"
    "${_sdk_test_root}/tests/code_image.cpp"
    "${_sdk_test_root}/tests/object_app.cpp"
    "${_sdk_test_root}/src/gpu/xenos/null/null_command_processor.cpp"
    "${_sdk_test_root}/src/gpu/xenos/null/null_xenos_graphics_driver.cpp"
    "${_sdk_test_root}/src/gpu/xenos/vulkan/tessellation_glsl.cpp")
foreach(_module rexglue rexcodegen rexkernel rexxam rexxbdm rexapp rexhid rexapu rexui rexfilesystem)
  target_sources(${_sdk_checks} PRIVATE $<TARGET_OBJECTS:${_module}-objects>)
endforeach()
target_include_directories(${_sdk_checks} PRIVATE
    "${_sdk_test_root}/src" "${_sdk_test_root}/tests" "${_sdk_test_root}/tests/unit"
    "${_sdk_test_root}/thirdparty" "${_sdk_test_root}/thirdparty/disasm")
target_include_directories(${_sdk_checks} SYSTEM PRIVATE
    "${_sdk_test_root}/thirdparty/cli11/include"
    $<TARGET_PROPERTY:rexcodegen-objects,INTERFACE_SYSTEM_INCLUDE_DIRECTORIES>)
target_compile_definitions(${_sdk_checks} PRIVATE
    REX_BUILDING_KERNEL REX_BUILDING_XAM REX_BUILDING_APP REX_BUILDING_HID
    REX_BUILDING_UI REX_BUILDING_APU REX_GPU_XENOS_STATIC)
target_link_libraries(${_sdk_checks} PRIVATE
    rexgpu-xenos-base rexppc excrypt mspack rexgpu rexbase simde disasm tlsf
    libavcodec libavutil SDL3::SDL3 SDL3::Headers Vulkan::Headers tomlplusplus
    xxHash::xxhash x360n_core Catch2::Catch2WithMain)
  if(WIN32)
    target_link_libraries(${_sdk_checks} PRIVATE ws2_32 winmm)
  endif()
  target_link_libraries(${_sdk_checks} PRIVATE glslang::SPIRV)
endforeach()
# Stage after all SDK dependencies are known, including its host allocator forwarder.
x360n_stage_runtime_dlls(x360n_sdk_checks)

target_sources(x360n_sdk_runtime_checks PRIVATE sdkcheck/workspace_lifetime_test.cpp
    sdkcheck/audio_callback_context_test.cpp
    sdkcheck/input_lifetime_serialization_test.cpp sdkcheck/launch_request_callback_test.cpp
    sdkcheck/offline_achievement_enumeration_test.cpp sdkcheck/offline_achievement_tiles_test.cpp
    sdkcheck/offline_achievement_writes_test.cpp sdkcheck/optional_perf_test.cpp
    sdkcheck/service_contracts_test.cpp sdkcheck/storage_catalog_test.cpp
    sdkcheck/storage_transfer_test.cpp sdkcheck/session_title_app_test.cpp)
set_source_files_properties(sdkcheck/dashboard_uri_test.cpp sdkcheck/xam_common_string_test.cpp
    ../titles/dash_17559/src/launch_uri.cpp PROPERTIES
    COMPILE_DEFINITIONS __imp__XamLaunchURI=x360n_test_XamLaunchURI)
set_source_files_properties(sdkcheck/dashboard_typeface_test.cpp ../titles/dash_17559/src/typeface.cpp
    PROPERTIES COMPILE_DEFINITIONS
    "__imp__XamGetLanguageTypeface=x360n_test_XamGetLanguageTypeface;__imp__XamGetLanguageTypefacePatch=x360n_test_XamGetLanguageTypefacePatch")
set_source_files_properties(sdkcheck/original_image_service_test.cpp sdkcheck/png_17559_service_test.cpp
    ../titles/dash_17559/src/system_ui_images.cpp PROPERTIES COMPILE_DEFINITIONS
    "__imp__XamReadImage=x360n_test_XamReadImage;__imp__XamInitializeGameTileCache=x360n_test_XamInitializeGameTileCache;__imp__XamDestroyGameTileCache=x360n_test_XamDestroyGameTileCache;__imp__XamReadGameTileImage=x360n_test_XamReadGameTileImage;__imp__XamPngEncode=x360n_test_XamPngEncode;__imp__XamPngEncodeEx=x360n_test_XamPngEncodeEx")
if(WIN32)
  target_link_libraries(x360n_sdk_runtime_checks PRIVATE ws2_32 winmm)
  add_library(x360n_session_ui_fixture SHARED
      ../titles/dash_17559/src/session_ui_host.cpp
      ../titles/dash_17559/src/system_ui.cpp ../titles/common/dashboard_assets.cpp)
  target_compile_definitions(x360n_session_ui_fixture PRIVATE X360N_SYSTEM_UI_PLUGIN=1)
  target_link_libraries(x360n_session_ui_fixture PRIVATE ${X360N_SDK_LIBRARIES} x360n_core)
  target_compile_definitions(x360n_sdk_runtime_checks PRIVATE
      X360N_TEST_SYSTEM_UI_HOST="$<TARGET_FILE:x360n_session_ui_fixture>")
  add_dependencies(x360n_sdk_runtime_checks x360n_session_ui_fixture)
elseif(APPLE)
  target_link_libraries(x360n_sdk_runtime_checks PRIVATE "-framework CoreFoundation")
endif()
if(WIN32)
  target_compile_definitions(x360n_sdk_runtime_checks PRIVATE
      X360N_SHUTDOWN_PROBE="$<TARGET_FILE:x360n_shutdown_probe>")
  add_dependencies(x360n_sdk_runtime_checks x360n_shutdown_probe)
endif()

add_executable(x360n_firmware_exit_probe sdkcheck/firmware_exit_probe.cpp ../titles/common/launch_handoff.cpp)
add_executable(x360n_input_hotplug_probe sdkcheck/input_hotplug_probe.cpp)
target_link_libraries(x360n_input_hotplug_probe PRIVATE ${X360N_SDK_LIBRARIES})
x360n_stage_runtime_dlls(x360n_input_hotplug_probe)
add_test(NAME sdkcheck.input-hotplug-concurrency COMMAND x360n_input_hotplug_probe)
set_tests_properties(sdkcheck.input-hotplug-concurrency PROPERTIES LABELS project TIMEOUT 30)
add_executable(x360n_sdl_hotplug_probe sdkcheck/sdl_hotplug_probe.cpp
    ${PROJECT_SOURCE_DIR}/external/rexglue-sdk/src/hid/sdl3/sdl3_input_driver.cpp)
# Driver and SDL must share one instance in this process.
target_link_libraries(x360n_sdl_hotplug_probe PRIVATE ${X360N_SDK_LIBRARIES})
target_link_libraries(x360n_sdl_hotplug_probe PRIVATE rexcodegen SDL3::SDL3 SDL3::Headers)
target_include_directories(x360n_sdl_hotplug_probe PRIVATE "${PROJECT_SOURCE_DIR}/external/rexglue-sdk/src")
target_compile_options(x360n_sdl_hotplug_probe PRIVATE
    -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
x360n_stage_runtime_dlls(x360n_sdl_hotplug_probe)
add_test(NAME sdkcheck.sdl-hotplug-events COMMAND x360n_sdl_hotplug_probe)
set_tests_properties(sdkcheck.sdl-hotplug-events PROPERTIES LABELS project TIMEOUT 30)
target_link_libraries(x360n_firmware_exit_probe PRIVATE ${X360N_SDK_LIBRARIES})
target_link_libraries(x360n_firmware_exit_probe PRIVATE rexcodegen)
target_include_directories(x360n_firmware_exit_probe PRIVATE "${PROJECT_SOURCE_DIR}/external/rexglue-sdk/src")
x360n_stage_runtime_dlls(x360n_firmware_exit_probe)
if(WIN32)
  add_test(NAME sdkcheck.firmware-exit COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File ${CMAKE_CURRENT_SOURCE_DIR}/sdkcheck/firmware_exit_test.ps1
    -Executable $<TARGET_FILE:x360n_firmware_exit_probe>)
  set_tests_properties(sdkcheck.firmware-exit PROPERTIES LABELS project TIMEOUT 30)
endif()
# Runtime DLLs staged on every build, not only when this executable relinks.
x360n_stage_runtime_dlls(x360n_sdk_runtime_checks)
if(WIN32)
  add_executable(x360n_1888_allocation_probe EXCLUDE_FROM_ALL tools/aot_xam_allocation_probe.cpp)
  target_sources(x360n_1888_allocation_probe PRIVATE
      ../titles/dash_1888/src/system_ui.cpp ../titles/common/dashboard_assets.cpp)
  target_compile_definitions(x360n_1888_allocation_probe PRIVATE X360N_1888_UI_DIAGNOSTIC=1)
  target_link_libraries(x360n_1888_allocation_probe PRIVATE rexcodegen)
  target_include_directories(x360n_1888_allocation_probe PRIVATE ${X360_REXGLUE_DIR}/src)
  target_link_libraries(x360n_1888_allocation_probe PRIVATE ${X360N_SDK_LIBRARIES} x360n_core)
  target_compile_options(x360n_1888_allocation_probe PRIVATE
      -UTRACY_ENABLE -UREXGLUE_ENABLE_PROFILING -UREXGLUE_ENABLE_PERF_COUNTERS)
  x360n_stage_runtime_dlls(x360n_1888_allocation_probe)
endif()
catch_discover_tests(x360n_sdk_runtime_checks TEST_PREFIX "sdkcheck." PROPERTIES LABELS "project" TIMEOUT 120
    DISCOVERY_MODE PRE_TEST)
if(WIN32)
  add_test(NAME sdkcheck.profile-pictures-processes COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File ${CMAKE_CURRENT_SOURCE_DIR}/local/profile-pictures-process.ps1
    -Checks $<TARGET_FILE:x360n_sdk_runtime_checks>)
  set_tests_properties(sdkcheck.profile-pictures-processes PROPERTIES LABELS project TIMEOUT 75)
  add_test(NAME sdkcheck.offline-achievements-processes COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File ${CMAKE_CURRENT_SOURCE_DIR}/local/offline-achievements-process.ps1
    -Executable $<TARGET_FILE:x360n_sdk_runtime_checks>)
  set_tests_properties(sdkcheck.offline-achievements-processes PROPERTIES LABELS project TIMEOUT 50)
  add_test(NAME sdkcheck.save-storage-processes COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File ${CMAKE_CURRENT_SOURCE_DIR}/local/save-storage-process.ps1
    -Executable $<TARGET_FILE:x360n_sdk_runtime_checks>)
  set_tests_properties(sdkcheck.save-storage-processes PROPERTIES LABELS project TIMEOUT 75)
endif()

# Keep the production original-system hooks distinct from the canonical SDK export bodies.
set_property(SOURCE sdkcheck/original_xam_service_test.cpp
    sdkcheck/xam_common_string_test.cpp ../titles/dash_17559/src/original_xam_services.cpp
    APPEND PROPERTY COMPILE_DEFINITIONS
    __imp__XamUniSortCmpString=x360n_test_XamUniSortCmpString
    __imp__XamContentGetLocalizedString=x360n_test_XamContentGetLocalizedString
    __imp__XMsgInProcessCall=x360n_test_XMsgInProcessCall
    __imp__XamLookupCommonStringByIndex=x360n_test_XamLookupCommonStringByIndex
    __imp__XMsgCancelIORequest=x360n_test_XMsgCancelIORequest
    __imp__XamContentRegisterChangeCallback=x360n_test_XamContentRegisterChangeCallback)
set_property(SOURCE sdkcheck/avatar_profile_manifest_test.cpp
    ../titles/common/avatar/avatar_profile_manifest.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    __imp__XamAvatarGetManifestLocalUser=x360n_test_XamAvatarGetManifestLocalUser
    __imp__XamAvatarSetManifest=x360n_test_XamAvatarSetManifest)

set_property(SOURCE sdkcheck/dashboard_camera_test.cpp ../titles/common/camera_state.cpp
    ../titles/dash_17559/src/unresolved_imports.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    __imp__XUsbcamGetState=x360n_test_XUsbcamGetState
    __imp__XUsbcamCreate=x360n_test_XUsbcamCreate
    __imp__XUsbcamSetCaptureMode=x360n_test_XUsbcamSetCaptureMode
    __imp__XUsbcamGetConfig=x360n_test_XUsbcamGetConfig
    __imp__XUsbcamSetConfig=x360n_test_XUsbcamSetConfig
    __imp__XUsbcamReadFrame=x360n_test_XUsbcamReadFrame
    __imp__XUsbcamSetView=x360n_test_XUsbcamSetView
    __imp__XUsbcamDestroy=x360n_test_XUsbcamDestroy)

# Original 1888 ABI adapters coexist with the canonical SDK exports in this test.
set_source_files_properties(sdkcheck/dashboard_1888_bridge_test.cpp
    ../titles/dash_1888/src/system_ui_app_load.cpp
    ../titles/dash_1888/src/legacy_ui_requests.cpp APPEND PROPERTIES COMPILE_DEFINITIONS
    "__imp__XamAppRequestLoad=x360n_1888_test_XamAppRequestLoad;__imp__XamAppUnrequestLoad=x360n_1888_test_XamAppUnrequestLoad;__imp__XamAppLoad=x360n_1888_test_XamAppLoad;__imp__XamShowCreateProfileUI=x360n_1888_test_XamShowCreateProfileUI;__imp__XamShowGamerCardUI=x360n_1888_test_XamShowGamerCardUI;__imp__XamProfileOpen=x360n_1888_test_XamProfileOpen")
