# The preparation model is independent of the frontend and title runtime.
add_library(x360n_desktop_model STATIC model/catalog.cpp model/workspace.cpp model/library.cpp model/identify.cpp model/import_source.cpp model/imported_games.cpp model/generic_aot.cpp model/update.cpp model/update_transaction.cpp model/handheld.cpp)
target_sources(x360n_desktop_model PRIVATE ../supervisor/launch_preflight.cpp)
target_sources(x360n_desktop_model PRIVATE ../content/system_update_probe.cpp)
target_include_directories(x360n_desktop_model PRIVATE ${X360_REXGLUE_DIR}/thirdparty/inja/third_party/include)
target_link_libraries(x360n_desktop_model PUBLIC x360n_core PRIVATE tomlplusplus)
target_include_directories(x360n_desktop_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/model)
# The supervisor owns launch preflight so one protected digest transaction can
# cover catalog, original library, receipts and process identity reporting.
target_link_libraries(x360n PRIVATE x360n_desktop_model)
target_include_directories(x360n PRIVATE ${X360_REXGLUE_DIR}/thirdparty/inja/third_party/include)
if(NOT X360_SUPERVISOR_ONLY)
  # Shared launcher state, rules and operations: one controller for every system.
  add_library(x360n_desktop_app STATIC app/app_controller.cpp app/operations.cpp app/environment_common.cpp)
  if(WIN32)
    target_sources(x360n_desktop_app PRIVATE app/environment_win32.cpp)
    target_link_libraries(x360n_desktop_app PRIVATE d3d12 dxgi advapi32 shell32 ole32)
  else()
    target_sources(x360n_desktop_app PRIVATE app/environment_posix.cpp)
    target_link_libraries(x360n_desktop_app PRIVATE ${CMAKE_DL_LIBS})
  endif()
  target_include_directories(x360n_desktop_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/app)
  target_include_directories(x360n_desktop_app PRIVATE ${X360_REXGLUE_DIR}/thirdparty/vulkan-headers/include)
  target_link_libraries(x360n_desktop_app PUBLIC x360n_desktop_model)
  if(X360_DISTRIBUTION)
    string(LENGTH "${X360_PACKAGE_PAYLOAD_SHA256}" _payload_length)
    if(NOT _payload_length EQUAL 64 OR NOT X360_PACKAGE_PAYLOAD_SHA256 MATCHES "^[0-9a-f]+$")
      message(FATAL_ERROR "Distribution requires the verified source payload identity")
    endif()
    target_compile_definitions(x360n_desktop_app PUBLIC X360N_DISTRIBUTION=1 X360N_PACKAGE_PAYLOAD="${X360_PACKAGE_PAYLOAD_SHA256}")
  endif()

  # The one graphical frontend (SDL3 + Dear ImGui). Built in desktop-only mode
  # without the title runtime; no title or SDK runtime library is linked.
  include(${PROJECT_SOURCE_DIR}/cmake/desktop/x360n_launcher_deps.cmake)
  add_library(x360n_launcher_ui STATIC ui/theme.cpp ui/icons.cpp ui/components.cpp ui/frontend.cpp ui/soft_raster.cpp ui/renderer.cpp
    ui/pages/home.cpp ui/pages/library.cpp ui/pages/dashboard.cpp ui/pages/import.cpp ui/pages/settings.cpp ui/pages/diagnostics.cpp)
  target_include_directories(x360n_launcher_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ui)
  target_link_libraries(x360n_launcher_ui PUBLIC x360n_desktop_app x360n_launcher_imgui)
  x360n_embed_file(x360n_launcher_ui x360n_font_noto_regular "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/NotoSansCJKjp-Regular.otf")
  x360n_embed_file(x360n_launcher_ui x360n_font_noto_medium "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/NotoSansCJKjp-Medium.otf")

  if(APPLE)
    add_executable(x360-native MACOSX_BUNDLE ui/main.cpp)
    set_target_properties(x360-native PROPERTIES MACOSX_BUNDLE_BUNDLE_NAME "x360-native"
      MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.fabegalo.x360-native" MACOSX_BUNDLE_SHORT_VERSION_STRING "${X360N_VERSION}")
  elseif(WIN32)
    # The version comes from cmake/distribution/x360n_version.cmake; x360n_core
    # already defines X360N_VERSION, the resource compiler gets it from this header.
    configure_file(resources/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/x360n_version.h @ONLY)
    enable_language(RC)
    add_executable(x360-native WIN32 ui/main.cpp resources/desktop.rc)
    target_include_directories(x360-native PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
    target_link_options(x360-native PRIVATE "LINKER:/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/resources/desktop.manifest")
    target_link_libraries(x360-native PRIVATE dwmapi)
  else()
    add_executable(x360-native ui/main.cpp)
  endif()
  target_link_libraries(x360-native PRIVATE x360n_launcher_ui)
  if(TARGET x360n-extract)
    add_dependencies(x360-native x360n x360n-extract)
  else()
    add_dependencies(x360-native x360n)
  endif()
  # Distribution copies each original target directly into the official ZIP
  # layout; no runnable launcher is needed inside its private build directory.
  if(NOT X360_DISTRIBUTION)
  # Dependencies can change without relinking the launcher. Stage the tools on
  # every launcher build so an old adjacent supervisor is never left behind.
  set(_stage_tools COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:x360n> $<TARGET_FILE_DIR:x360-native>)
  set(_stage_depends x360n)
  if(TARGET x360n-extract)
    list(APPEND _stage_tools COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:x360n-extract> $<TARGET_FILE_DIR:x360-native>)
    list(APPEND _stage_depends x360n-extract)
  endif()
  add_custom_target(x360n_stage_desktop_tools ${_stage_tools} DEPENDS ${_stage_depends})
  add_dependencies(x360-native x360n_stage_desktop_tools)
  if(TARGET x360n-mspack)
    add_custom_target(x360n_stage_update_decoder
      COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:x360n-mspack> $<TARGET_FILE_DIR:x360-native>
      DEPENDS x360n-mspack)
    add_dependencies(x360n_stage_desktop_tools x360n_stage_update_decoder)
  endif()
  endif()
endif()
if(NOT X360_SUPERVISOR_ONLY)
  include(${PROJECT_SOURCE_DIR}/tests/desktop/targets.cmake)
endif()
