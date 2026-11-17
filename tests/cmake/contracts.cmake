# Included from tests/CMakeLists.txt; paths and target outputs stay rooted there.
add_subdirectory(ppc_smoke)

# Independent synthetic LDI import contract (no proprietary input).
add_executable(x360n_ldi_checks sdkcheck/ldi_test.cpp)
target_link_libraries(x360n_ldi_checks PRIVATE ${X360N_SDK_LIBRARIES})
target_link_libraries(x360n_ldi_checks PRIVATE rexcodegen)
target_include_directories(x360n_ldi_checks PRIVATE "${PROJECT_SOURCE_DIR}/external/rexglue-sdk/src"
    "${PROJECT_SOURCE_DIR}/external/rexglue-sdk/tests")
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
add_subdirectory(sdkcheck/vmsum)
add_subdirectory(sdkcheck/cr_alias)

# Explicit manual diagnostic only; never part of automatic CTest runs.
if(WIN32)
  add_executable(x360n_xenos_cycle_probe EXCLUDE_FROM_ALL local/xenos_cycle_probe.cpp)
  target_link_libraries(x360n_xenos_cycle_probe PRIVATE x360n_core)
endif()

if(TARGET cross_module_tests)
  add_library(x360n_session_ui_contract SHARED
    "${PROJECT_SOURCE_DIR}/titles/dash_17559/src/session_ui_host.cpp"
    "${PROJECT_SOURCE_DIR}/titles/dash_17559/src/system_ui.cpp"
    "${PROJECT_SOURCE_DIR}/titles/common/dashboard_assets.cpp")
  target_compile_definitions(x360n_session_ui_contract PRIVATE X360N_SYSTEM_UI_PLUGIN=1)
  target_link_libraries(x360n_session_ui_contract PRIVATE rex::sdk x360n_core)
  target_sources(cross_module_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/tests/sdkcheck/session_title_app_test.cpp"
    "${PROJECT_SOURCE_DIR}/titles/common/launch_handoff.cpp"
    "${PROJECT_SOURCE_DIR}/titles/common/guest_fault_context.cpp"
    "${PROJECT_SOURCE_DIR}/titles/common/dashboard_assets.cpp")
  target_link_libraries(cross_module_tests PRIVATE rex::sdk x360n_core)
  set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/sdkcheck/session_title_app_test.cpp"
    TARGET_DIRECTORY cross_module_tests PROPERTIES COMPILE_OPTIONS -fchar8_t SKIP_PRECOMPILE_HEADERS ON
    COMPILE_DEFINITIONS "X360N_TEST_SYSTEM_UI_HOST=\"$<TARGET_FILE:x360n_session_ui_contract>\"")
  set_source_files_properties("${PROJECT_SOURCE_DIR}/titles/common/launch_handoff.cpp"
    TARGET_DIRECTORY cross_module_tests PROPERTIES COMPILE_OPTIONS -fchar8_t SKIP_PRECOMPILE_HEADERS ON)
  add_dependencies(cross_module_tests x360n_session_ui_contract)
endif()

# Ported regressions use the SDK's own coherent kernel fixture and object table.
if(TARGET unit_tests)
  set(root "${PROJECT_SOURCE_DIR}")
  # Compile the generic host against the real SDK APIs without claiming guest execution.
  set(TITLE x360n_generic_compile)
  set(HOST_DISPATCH 1)
  set(_host_check "${CMAKE_CURRENT_BINARY_DIR}/generic-host")
  file(MAKE_DIRECTORY "${_host_check}")
  configure_file("${root}/titles/common/generic/main.cpp" "${_host_check}/main.cpp" @ONLY)
  file(WRITE "${_host_check}/${TITLE}_init.h"
    "#include <rex/ppc/image.h>\ninline constexpr rex::ppc::PPCImage kPPCImageConfig{};\n")
  add_library(x360n_generic_host_compile OBJECT "${_host_check}/main.cpp")
  target_include_directories(x360n_generic_host_compile PRIVATE "${_host_check}" "${root}/titles/common")
  target_compile_definitions(x360n_generic_host_compile PRIVATE X360N_HAS_NATIVE_GPU=1)
  target_link_libraries(x360n_generic_host_compile PRIVATE rex::sdk)
  add_dependencies(unit_tests x360n_generic_host_compile)
  file(WRITE "${_host_check}/dash6717.cpp"
    "#include <rex/ppc/image.h>\ninline constexpr rex::ppc::PPCImage kPPCImageConfig{};\n#include \"${root}/titles/dash_6717/src/dash_6717_app.h\"\nvoid CheckDashboard6717Host() { Dash6717App app; }\n")
  target_sources(x360n_generic_host_compile PRIVATE "${_host_check}/dash6717.cpp")
  file(WRITE "${_host_check}/dash17559.cpp"
    "#include <rex/ppc/image.h>\ninline constexpr rex::ppc::PPCImage kPPCImageConfig{};\n#include \"${root}/titles/dash_17559/src/dash_17559_app.h\"\nvoid CheckDashboard17559Host() { Dash17559App app; }\n")
  target_sources(x360n_generic_host_compile PRIVATE "${_host_check}/dash17559.cpp")
  file(WRITE "${_host_check}/dash1888.cpp"
    "#include <rex/ppc/image.h>\ninline constexpr rex::ppc::PPCImage kPPCImageConfig{};\n#include \"${root}/titles/dash_1888/src/dash_1888_app.h\"\nvoid CheckDashboard1888Host() { Dash1888App app; }\n")
  target_sources(x360n_generic_host_compile PRIVATE "${_host_check}/dash1888.cpp"
    "${root}/titles/dash_1888/src/boot_intro.cpp" "${root}/titles/dash_1888/src/boot_intro_control.cpp"
    "${root}/titles/dash_1888/src/system_ui.cpp" "${root}/titles/dash_1888/src/system_ui_imports.cpp"
    "${root}/titles/dash_1888/src/system_ui_app_load.cpp" "${root}/titles/dash_1888/src/system_ui_memory.cpp"
    "${root}/titles/dash_1888/src/legacy_ui_requests.cpp" "${root}/titles/dash_1888/src/vk_registration.cpp")
  set_source_files_properties("${root}/titles/dash_1888/src/system_ui.cpp"
    TARGET_DIRECTORY x360n_generic_host_compile PROPERTIES COMPILE_DEFINITIONS X360N_1888_UI_DIAGNOSTIC=1)
  target_sources(x360n_generic_host_compile PRIVATE "${root}/titles/dash_17559/src/system_ui.cpp")
  target_sources(x360n_generic_host_compile PRIVATE "${root}/titles/dash_17559/src/original_xam_services.cpp" "${root}/titles/dash_17559/src/system_ui_imports.cpp")
  target_sources(x360n_generic_host_compile PRIVATE "${root}/titles/dash_17559/src/system_ui_memory.cpp"
    "${root}/titles/dash_17559/src/createprofile_imports.cpp" "${root}/titles/dash_17559/src/vk_imports.cpp"
    "${root}/titles/dash_17559/src/system_ui_images.cpp")
  target_sources(unit_tests PRIVATE "${root}/titles/common/avatar/avatar_profile_manifest.cpp"
    "${root}/tests/sdkcheck/avatar_profile_manifest_test.cpp")
  set_source_files_properties("${root}/titles/common/avatar/avatar_profile_manifest.cpp"
    "${root}/tests/sdkcheck/avatar_profile_manifest_test.cpp" TARGET_DIRECTORY unit_tests PROPERTIES
    COMPILE_DEFINITIONS "__imp__XamAvatarGetManifestLocalUser=x360n_test_XamAvatarGetManifestLocalUser;__imp__XamAvatarSetManifest=x360n_test_XamAvatarSetManifest")
  target_sources(x360n_generic_host_compile PRIVATE "${root}/titles/common/avatar/avatar_aot_assets.cpp"
    "${root}/titles/common/avatar/avatar_aot_catalog.cpp"
    "${root}/titles/common/avatar/avatar_service_17559.cpp"
    "${root}/titles/dash_17559/src/typeface.cpp" "${root}/titles/dash_17559/src/signin_imports.cpp"
    "${root}/titles/dash_17559/src/unresolved_imports.cpp" "${root}/titles/common/camera_state.cpp")
  file(APPEND "${_host_check}/dash17559.cpp"
    "#include \"${root}/titles/dash_17559/src/storage_selector_diagnostic.h\"\n#include \"${root}/titles/dash_17559/src/message_box_diagnostic.h\"\n#include \"${root}/titles/dash_17559/src/keyboard_diagnostic.h\"\n")
  target_sources(unit_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/tests/sdkcheck/service_contracts_test.cpp"
    "${PROJECT_SOURCE_DIR}/tests/sdkcheck/system_ui_context_test.cpp"
    "${PROJECT_SOURCE_DIR}/tests/sdkcheck/system_ui_transition_test.cpp"
    "${PROJECT_SOURCE_DIR}/tests/sdkcheck/system_ui_compositor_test.cpp"
    "${PROJECT_SOURCE_DIR}/tests/sdkcheck/system_ui_task_test.cpp"
    "${PROJECT_SOURCE_DIR}/tests/sdkcheck/system_ui_input_test.cpp"
    "${PROJECT_SOURCE_DIR}/tests/sdkcheck/package_mount_test.cpp"
    "${PROJECT_SOURCE_DIR}/tests/sdkcheck/dashboard_uri_test.cpp"
    "${PROJECT_SOURCE_DIR}/tests/sdkcheck/dashboard_region_test.cpp"
    "${PROJECT_SOURCE_DIR}/titles/dash_17559/src/launch_uri.cpp"
    "${PROJECT_SOURCE_DIR}/titles/dash_17559/src/boot_intro.cpp"
    "${PROJECT_SOURCE_DIR}/titles/dash_17559/src/display_transition.cpp"
    "${PROJECT_SOURCE_DIR}/tests/sdkcheck/persist_display_test.cpp"
    "${PROJECT_SOURCE_DIR}/titles/common/dashboard_assets.cpp"
    "${PROJECT_SOURCE_DIR}/tests/sdkcheck/session_title_app_test.cpp"
    "${PROJECT_SOURCE_DIR}/tests/sdkcheck/presenter_effects_test.cpp"
    "${PROJECT_SOURCE_DIR}/titles/common/launch_handoff.cpp"
    "${PROJECT_SOURCE_DIR}/titles/common/guest_fault_context.cpp")
  set_source_files_properties("${PROJECT_SOURCE_DIR}/titles/common/launch_handoff.cpp"
    TARGET_DIRECTORY unit_tests PROPERTIES COMPILE_OPTIONS -fchar8_t SKIP_PRECOMPILE_HEADERS ON)
  set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/sdkcheck/session_title_app_test.cpp"
    TARGET_DIRECTORY unit_tests PROPERTIES COMPILE_OPTIONS -fchar8_t SKIP_PRECOMPILE_HEADERS ON)
  # Unit tests embed all kernel stubs; isolate this profile hook's linker name.
  set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/sdkcheck/dashboard_uri_test.cpp"
    "${PROJECT_SOURCE_DIR}/titles/dash_17559/src/launch_uri.cpp"
    TARGET_DIRECTORY unit_tests PROPERTIES
      COMPILE_DEFINITIONS __imp__XamLaunchURI=x360n_test_XamLaunchURI)
  target_link_libraries(unit_tests PRIVATE x360n_core)
endif()
