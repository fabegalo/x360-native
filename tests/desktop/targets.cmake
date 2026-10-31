# Included in the src/desktop directory scope to preserve binary locations.
if(TARGET Catch2::Catch2WithMain)
  add_executable(x360n_first_preparation_probe EXCLUDE_FROM_ALL ../../tests/desktop/first_preparation_probe.cpp)
  target_link_libraries(x360n_first_preparation_probe PRIVATE x360n_desktop_model)
  target_include_directories(x360n_first_preparation_probe PRIVATE ${X360_REXGLUE_DIR}/thirdparty/inja/third_party/include)
  add_executable(x360n_preparation_benchmark EXCLUDE_FROM_ALL ../../tests/desktop/preparation_benchmark.cpp)
  target_link_libraries(x360n_preparation_benchmark PRIVATE x360n_core)
  add_executable(x360n_launch_benchmark EXCLUDE_FROM_ALL ../../tests/desktop/launch_benchmark.cpp)
  target_link_libraries(x360n_launch_benchmark PRIVATE x360n_desktop_model)
  add_executable(x360n_launch_smoke EXCLUDE_FROM_ALL ../../tests/desktop/launch_smoke.cpp)
  target_link_libraries(x360n_launch_smoke PRIVATE x360n_desktop_model)
  if(X360N_PREFLIGHT_BASELINE_DIR)
    add_executable(x360n_preflight_baseline EXCLUDE_FROM_ALL ../../tests/desktop/launch_benchmark.cpp
      ../../tests/desktop/preflight_baseline.cpp
      "${X360N_PREFLIGHT_BASELINE_DIR}/digest_batch.cpp"
      "${X360N_PREFLIGHT_BASELINE_DIR}/preparation.cpp")
    target_link_libraries(x360n_preflight_baseline PRIVATE x360n_desktop_model tomlplusplus)
    target_include_directories(x360n_preflight_baseline PRIVATE ${X360_REXGLUE_DIR}/thirdparty/inja/third_party/include)
    add_executable(x360n_preflight_baseline_optimized EXCLUDE_FROM_ALL ../../tests/desktop/launch_benchmark.cpp
      ../../tests/desktop/preflight_baseline.cpp
      "${X360N_PREFLIGHT_BASELINE_DIR}/digest_batch-optimized.cpp"
      "${X360N_PREFLIGHT_BASELINE_DIR}/preparation-optimized.cpp")
    set_source_files_properties(
      "${X360N_PREFLIGHT_BASELINE_DIR}/digest_batch-optimized.cpp"
      "${X360N_PREFLIGHT_BASELINE_DIR}/preparation-optimized.cpp" PROPERTIES
      COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
    target_link_libraries(x360n_preflight_baseline_optimized PRIVATE x360n_desktop_model tomlplusplus)
    target_include_directories(x360n_preflight_baseline_optimized PRIVATE ${X360_REXGLUE_DIR}/thirdparty/inja/third_party/include)
  endif()
  if(X360N_BASELINE_CORE_DIR)
    # Reproduce the old verifier with the same benchmark/flags. Explicit source
    # objects satisfy these symbols before the current static archive is read.
    add_executable(x360n_launch_baseline EXCLUDE_FROM_ALL ../../tests/desktop/launch_benchmark.cpp
      ../../tests/desktop/launch_baseline_paths.cpp
      ../../tests/desktop/preflight_baseline.cpp
      "${X360N_BASELINE_CORE_DIR}/digest_batch-before.cpp"
      "${X360N_BASELINE_CORE_DIR}/preparation-before.cpp")
    target_link_libraries(x360n_launch_baseline PRIVATE x360n_desktop_model tomlplusplus)
    target_include_directories(x360n_launch_baseline PRIVATE ${X360_REXGLUE_DIR}/thirdparty/inja/third_party/include)
  endif()
  add_executable(x360n_launcher_benchmark EXCLUDE_FROM_ALL ../../tests/desktop/launcher_benchmark.cpp)
  target_link_libraries(x360n_launcher_benchmark PRIVATE x360n_desktop_model)
  target_compile_definitions(x360n_launcher_benchmark PRIVATE X360N_BENCHMARK_BATCH=1)
  # The shared launcher (controller and headless frontend) is tested on every system.
  add_executable(x360n_launcher_child ../../tests/desktop/launcher_child.cpp)
  target_link_libraries(x360n_launcher_child PRIVATE x360n_desktop_model)
  add_test(NAME desktop.launcher-fonts COMMAND x360n_launcher_tests "[launcher-fonts]")
  set_tests_properties(desktop.launcher-fonts PROPERTIES LABELS project TIMEOUT 30)
  add_test(NAME desktop.launcher-usability COMMAND x360n_launcher_tests "[launcher-usability]")
  set_tests_properties(desktop.launcher-usability PROPERTIES LABELS project TIMEOUT 30)
  add_test(NAME desktop.launcher-icons COMMAND x360n_launcher_tests "[launcher-icons]")
  set_tests_properties(desktop.launcher-icons PROPERTIES LABELS project TIMEOUT 30)
  if(NOT WIN32)
    add_executable(x360n_launcher_tests ../../tests/desktop/launcher_ui_test.cpp)
    target_link_libraries(x360n_launcher_tests PRIVATE x360n_launcher_ui Catch2::Catch2WithMain)
    target_compile_definitions(x360n_launcher_tests PRIVATE X360N_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
      X360N_LAUNCHER_CHILD="$<TARGET_FILE:x360n_launcher_child>")
    target_include_directories(x360n_launcher_tests PRIVATE ${X360_REXGLUE_DIR}/thirdparty/inja/third_party/include)
    add_dependencies(x360n_launcher_tests x360n x360n_launcher_child)
    add_test(NAME desktop.launcher COMMAND x360n_launcher_tests "[launcher-ui],[launcher-options],[launcher-ready],[library-membership]")
    set_tests_properties(desktop.launcher PROPERTIES LABELS project TIMEOUT 90)
    add_test(NAME desktop.launcher-layout COMMAND x360n_launcher_tests "[launcher-layout]")
    set_tests_properties(desktop.launcher-layout PROPERTIES LABELS project TIMEOUT 300)
  endif()
  if(WIN32)
    add_executable(x360n_launcher_test_child ../../tests/supervisor/test_child.cpp)
    add_executable(x360n_launcher_session_child ../../tests/supervisor/session_child.cpp)
    target_link_libraries(x360n_launcher_session_child PRIVATE x360n_core)
    add_executable(x360n_launcher_tests ../../tests/desktop/launcher_ui_test.cpp
      ../../tests/supervisor/digest_batch_test.cpp ../../tests/supervisor/sha256_test.cpp
      ../../tests/supervisor/preparation_test.cpp ../../tests/supervisor/process_test.cpp
      ../../tests/supervisor/session_process_test.cpp)
    # The shared controller and the one frontend, driven without a window.
    target_link_libraries(x360n_launcher_tests PRIVATE x360n_launcher_ui Catch2::Catch2WithMain)
    target_compile_definitions(x360n_launcher_tests PRIVATE UNICODE _UNICODE
      X360N_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
      X360N_TEST_CHILD="$<TARGET_FILE:x360n_launcher_test_child>"
      X360N_LAUNCHER_CHILD="$<TARGET_FILE:x360n_launcher_child>"
      X360N_SESSION_CHILD="$<TARGET_FILE:x360n_launcher_session_child>"
      X360N_EXE="$<TARGET_FILE:x360n>")
    target_include_directories(x360n_launcher_tests PRIVATE ${X360_REXGLUE_DIR}/thirdparty/inja/third_party/include)
    add_dependencies(x360n_launcher_tests x360n x360n_launcher_child x360n_launcher_test_child x360n_launcher_session_child)
    add_test(NAME desktop.launcher COMMAND x360n_launcher_tests "[launcher-ui],[launcher-options],[launcher-ready],[library-membership],[digest-batch],[session-event]")
    set_tests_properties(desktop.launcher PROPERTIES LABELS project TIMEOUT 90)
    add_test(NAME desktop.launcher-layout COMMAND x360n_launcher_tests "[launcher-layout]")
    # Every page at 1000x700 to 1920x1080 and 100/125/150/200% scale, headless.
    set_tests_properties(desktop.launcher-layout PROPERTIES LABELS project TIMEOUT 300)
    add_test(NAME desktop.session-fullscreen COMMAND x360n_launcher_tests "[session-fullscreen]")
    set_tests_properties(desktop.session-fullscreen PROPERTIES LABELS project TIMEOUT 30)
    add_test(NAME desktop.version-from-commits COMMAND powershell -NoProfile -ExecutionPolicy Bypass
      -File ${PROJECT_SOURCE_DIR}/tests/packaging/compute_version_test.ps1)
    set_tests_properties(desktop.version-from-commits PROPERTIES LABELS project TIMEOUT 60)
    add_test(NAME desktop.launcher-contracts COMMAND x360n_launcher_tests "[sha256],[preparation],[process],[session],[session-storage],[session-crash],[console-settings],[games-played],[avatar]")
    set_tests_properties(desktop.launcher-contracts PROPERTIES LABELS project TIMEOUT 120)
  endif()
  add_executable(x360n_desktop_tests ../../tests/desktop/catalog_test.cpp ../../tests/desktop/imported_games_test.cpp
    ../../tests/desktop/update_test.cpp ../../tests/desktop/handheld_test.cpp)
  add_executable(x360n_update_crash_probe ../../tests/desktop/update_crash_probe.cpp)
  target_link_libraries(x360n_update_crash_probe PRIVATE x360n_desktop_model)
  add_dependencies(x360n_desktop_tests x360n_update_crash_probe x360n)
  target_compile_definitions(x360n_desktop_tests PRIVATE
    X360N_UPDATE_PROBE="$<TARGET_FILE:x360n_update_crash_probe>"
    X360N_RECOVERY_EXE="$<TARGET_FILE:x360n>")
  target_include_directories(x360n_desktop_tests PRIVATE ${X360_REXGLUE_DIR}/thirdparty/inja/third_party/include)
  target_compile_definitions(x360n_desktop_tests PRIVATE X360N_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
  target_compile_definitions(x360n_desktop_tests PRIVATE X360N_TEST_SDK_ROOT="${X360_REXGLUE_DIR}")
  # Content import (extractor, ISO/STFS, System Update) and XEX crypto run on every system.
  if(TARGET x360n-extract)
    target_sources(x360n_desktop_tests PRIVATE ../../tests/content/extract_disc_test.cpp ../../tests/desktop/import_source_test.cpp
      ../../tests/content/system_update_test.cpp ../content/safe_zip.cpp
      ../../tests/content/xex_crypto_test.cpp ../content/xex_crypto.cpp)
    target_link_libraries(x360n_desktop_tests PRIVATE x360n_content_crypto)
    target_include_directories(x360n_desktop_tests PRIVATE ${X360N_UPDATE_ZIP_INCLUDE})
    target_compile_definitions(x360n_desktop_tests PRIVATE X360N_CONTENT_EXE="$<TARGET_FILE:x360n-extract>")
    add_dependencies(x360n_desktop_tests x360n-extract)
  endif()
  if(X360_DESKTOP_ONLY)
    target_sources(x360n_desktop_tests PRIVATE ../../tests/supervisor/console_settings_test.cpp)
    target_sources(x360n_desktop_tests PRIVATE ../../tests/supervisor/session_test.cpp ../../tests/supervisor/xex_identity_test.cpp ../../tests/supervisor/renderer_settings_test.cpp)
    target_compile_definitions(x360n_desktop_tests PRIVATE X360N_EXE="$<TARGET_FILE:x360n>")
    add_dependencies(x360n_desktop_tests x360n)
  endif()
  target_link_libraries(x360n_desktop_tests PRIVATE x360n_desktop_model Catch2::Catch2WithMain tomlplusplus)
  add_test(NAME desktop.catalog COMMAND x360n_desktop_tests "~[update-crash]")
  set_tests_properties(desktop.catalog PROPERTIES LABELS project TIMEOUT 60)
  add_test(NAME desktop.update-recovery COMMAND x360n_desktop_tests "[update-crash]")
  set_tests_properties(desktop.update-recovery PROPERTIES LABELS project TIMEOUT 300)
endif()
