# The preparation model is independent of the Windows frontend and title runtime.
add_library(x360n_desktop_model STATIC model/catalog.cpp model/workspace.cpp model/library.cpp model/identify.cpp model/import_source.cpp model/imported_games.cpp model/generic_aot.cpp model/update.cpp model/update_transaction.cpp)
target_sources(x360n_desktop_model PRIVATE ../supervisor/launch_preflight.cpp)
target_include_directories(x360n_desktop_model PRIVATE ${X360_REXGLUE_DIR}/thirdparty/inja/third_party/include)
target_link_libraries(x360n_desktop_model PUBLIC x360n_core PRIVATE tomlplusplus)
target_include_directories(x360n_desktop_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/model)
# The supervisor owns launch preflight so one protected digest transaction can
# cover catalog, original library, receipts and process identity reporting.
target_link_libraries(x360n PRIVATE x360n_desktop_model)
if(WIN32)
  # The version comes from cmake/distribution/x360n_version.cmake; x360n_core
  # already defines X360N_VERSION, the resource compiler gets it from this header.
  configure_file(resources/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/x360n_version.h @ONLY)
  enable_language(RC)
  add_executable(x360-native WIN32 win32/main_win32.cpp win32/environment_win32.cpp resources/desktop.rc)
  target_include_directories(x360-native PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
  target_link_options(x360-native PRIVATE "LINKER:/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/resources/desktop.manifest")
  target_link_libraries(x360-native PRIVATE x360n_desktop_model comctl32 ole32 shell32 uuid d3d12 dxgi xinput advapi32 dwmapi gdiplus uxtheme)
  target_compile_definitions(x360-native PRIVATE UNICODE _UNICODE)
  if(X360_DISTRIBUTION)
    string(LENGTH "${X360_PACKAGE_PAYLOAD_SHA256}" _payload_length)
    if(NOT _payload_length EQUAL 64 OR NOT X360_PACKAGE_PAYLOAD_SHA256 MATCHES "^[0-9a-f]+$")
      message(FATAL_ERROR "Distribution requires the verified source payload identity")
    endif()
    target_compile_definitions(x360-native PRIVATE X360N_DISTRIBUTION=1 X360N_PACKAGE_PAYLOAD="${X360_PACKAGE_PAYLOAD_SHA256}")
  endif()
  add_dependencies(x360-native x360n x360n-extract)
  # Distribution copies each original target directly into the official ZIP
  # layout; no runnable launcher is needed inside its private build directory.
  if(NOT X360_DISTRIBUTION)
  # Dependencies can change without relinking the launcher. Stage the tools on
  # every launcher build so an old adjacent supervisor is never left behind.
  add_custom_target(x360n_stage_desktop_tools
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:x360n> $<TARGET_FILE_DIR:x360-native>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:x360n-extract> $<TARGET_FILE_DIR:x360-native>
    DEPENDS x360n x360n-extract)
  add_dependencies(x360-native x360n_stage_desktop_tools)
  endif()
endif()
include(${PROJECT_SOURCE_DIR}/tests/desktop/targets.cmake)
