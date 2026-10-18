# Included in the src/desktop directory scope to preserve binary locations.
if(TARGET Catch2::Catch2WithMain)
  add_executable(x360n_launcher_benchmark EXCLUDE_FROM_ALL ../../tests/desktop/launcher_benchmark.cpp)
  target_link_libraries(x360n_launcher_benchmark PRIVATE x360n_desktop_model)
  target_compile_definitions(x360n_launcher_benchmark PRIVATE X360N_BENCHMARK_BATCH=1)
  if(WIN32)
    add_executable(x360n_launcher_child ../../tests/desktop/launcher_child.cpp)
    add_executable(x360n_launcher_test_child ../../tests/supervisor/test_child.cpp)
    add_executable(x360n_launcher_session_child ../../tests/supervisor/session_child.cpp)
    target_link_libraries(x360n_launcher_session_child PRIVATE x360n_core)
    add_executable(x360n_launcher_tests ../../tests/desktop/launcher_ui_test.cpp
      ../../tests/supervisor/digest_batch_test.cpp ../../tests/supervisor/sha256_test.cpp
      ../../tests/supervisor/preparation_test.cpp ../../tests/supervisor/process_test.cpp
      ../../tests/supervisor/session_process_test.cpp win32/environment_win32.cpp)
    target_link_libraries(x360n_launcher_tests PRIVATE x360n_desktop_model Catch2::Catch2WithMain
      comctl32 ole32 shell32 uuid d3d12 dxgi xinput advapi32 dwmapi gdiplus uxtheme)
    target_compile_definitions(x360n_launcher_tests PRIVATE UNICODE _UNICODE
      X360N_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
      X360N_TEST_CHILD="$<TARGET_FILE:x360n_launcher_test_child>"
      X360N_LAUNCHER_CHILD="$<TARGET_FILE:x360n_launcher_child>"
      X360N_SESSION_CHILD="$<TARGET_FILE:x360n_launcher_session_child>"
      X360N_EXE="$<TARGET_FILE:x360n>")
    target_include_directories(x360n_launcher_tests PRIVATE ${X360_REXGLUE_DIR}/thirdparty/inja/third_party/include)
    add_dependencies(x360n_launcher_tests x360n x360n_launcher_child x360n_launcher_test_child x360n_launcher_session_child)
    add_test(NAME desktop.launcher COMMAND x360n_launcher_tests "[launcher-ui],[digest-batch],[session-event]")
    set_tests_properties(desktop.launcher PROPERTIES LABELS project TIMEOUT 90)
    add_test(NAME desktop.launcher-contracts COMMAND x360n_launcher_tests "[sha256],[preparation],[process],[session],[session-storage],[session-crash],[console-settings],[games-played],[avatar]")
    set_tests_properties(desktop.launcher-contracts PROPERTIES LABELS project TIMEOUT 120)
  endif()
  add_executable(x360n_desktop_tests ../../tests/desktop/catalog_test.cpp ../../tests/desktop/imported_games_test.cpp)
  target_include_directories(x360n_desktop_tests PRIVATE ${X360_REXGLUE_DIR}/thirdparty/inja/third_party/include)
  target_compile_definitions(x360n_desktop_tests PRIVATE X360N_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
  if(WIN32)
    target_sources(x360n_desktop_tests PRIVATE ../../tests/content/extract_disc_test.cpp ../../tests/desktop/import_source_test.cpp)
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
  add_test(NAME desktop.catalog COMMAND x360n_desktop_tests)
  set_tests_properties(desktop.catalog PROPERTIES LABELS project TIMEOUT 60)
endif()
