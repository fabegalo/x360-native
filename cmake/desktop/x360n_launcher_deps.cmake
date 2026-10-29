# Launcher dependencies, all from the pinned SDK's thirdparty tree: SDL3 (Zlib),
# Dear ImGui with its SDL3/SDL_GPU backends (MIT), Vulkan headers for the
# environment probe (Apache-2.0) and the fonts Roboto/Cousine (Apache-2.0).
# The full build reuses the SDK's own SDL3/imgui targets; the desktop-only build
# configures the same SDL3 options here, without the title runtime.

# Embeds a binary file as `extern "C" const unsigned char <symbol>[]` and
# `<symbol>_size` in a generated C source added to `target`.
function(x360n_embed_file target symbol file)
  set(output "${CMAKE_CURRENT_BINARY_DIR}/embedded/${symbol}.c")
  add_custom_command(OUTPUT "${output}"
    COMMAND ${CMAKE_COMMAND} -DINPUT=${file} -DOUTPUT=${output} -DSYMBOL=${symbol}
            -P "${PROJECT_SOURCE_DIR}/cmake/desktop/x360n_embed_file.cmake"
    DEPENDS "${file}" "${PROJECT_SOURCE_DIR}/cmake/desktop/x360n_embed_file.cmake"
    COMMENT "Embedding ${symbol}" VERBATIM)
  target_sources(${target} PRIVATE "${output}")
endfunction()

if(NOT TARGET SDL3::SDL3)
  # Same switches as the SDK's thirdparty/CMakeLists.txt, so the launcher's SDL
  # matches the runtime's in every build mode.
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
  set(SDL_VIDEO ON CACHE BOOL "" FORCE)
  if(APPLE)
    set(SDL_METAL ON CACHE BOOL "" FORCE)
    set(SDL_VULKAN ON CACHE BOOL "" FORCE)
  else()
    set(SDL_METAL OFF CACHE BOOL "" FORCE)
    if(UNIX)
      set(SDL_VULKAN ON CACHE BOOL "" FORCE)
    else()
      set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
    endif()
  endif()
  set(SDL_RENDER OFF CACHE BOOL "" FORCE)
  set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
  set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
  set(SDL_POWER OFF CACHE BOOL "" FORCE)
  set(SDL_FILESYSTEM OFF CACHE BOOL "" FORCE)
  set(SDL_LOCALE ON CACHE BOOL "" FORCE)
  set(SDL_FILE OFF CACHE BOOL "" FORCE)
  set(SDL_MISC OFF CACHE BOOL "" FORCE)
  set(SDL_DIALOG ON CACHE BOOL "" FORCE)
  set(SDL_TRAY OFF CACHE BOOL "" FORCE)
  set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
  set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
  set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
  set(SDL_DISKAUDIO OFF CACHE BOOL "" FORCE)
  set(SDL_DUMMYAUDIO OFF CACHE BOOL "" FORCE)
  set(SDL_DUMMYVIDEO OFF CACHE BOOL "" FORCE)
  if(UNIX AND NOT APPLE)
    set(SDL_X11 ON CACHE BOOL "" FORCE)
    set(SDL_WAYLAND ON CACHE BOOL "" FORCE)
    set(SDL_ALSA ON CACHE BOOL "" FORCE)
    set(SDL_ALSA_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_PULSEAUDIO ON CACHE BOOL "" FORCE)
    set(SDL_PULSEAUDIO_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_PIPEWIRE ON CACHE BOOL "" FORCE)
    set(SDL_PIPEWIRE_SHARED ON CACHE BOOL "" FORCE)
  endif()
  add_subdirectory("${X360_REXGLUE_DIR}/thirdparty/sdl3" desktop-deps/sdl3)
endif()

set(X360N_IMGUI_DIR "${X360_REXGLUE_DIR}/thirdparty/imgui")
add_library(x360n_launcher_imgui STATIC
  "${X360N_IMGUI_DIR}/imgui.cpp" "${X360N_IMGUI_DIR}/imgui_draw.cpp"
  "${X360N_IMGUI_DIR}/imgui_tables.cpp" "${X360N_IMGUI_DIR}/imgui_widgets.cpp"
  "${X360N_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp" "${X360N_IMGUI_DIR}/backends/imgui_impl_sdlgpu3.cpp")
target_include_directories(x360n_launcher_imgui PUBLIC "${X360N_IMGUI_DIR}" "${X360N_IMGUI_DIR}/backends")
target_compile_definitions(x360n_launcher_imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS IMGUI_DEFINE_MATH_OPERATORS)
target_link_libraries(x360n_launcher_imgui PUBLIC SDL3::SDL3)
target_compile_options(x360n_launcher_imgui PRIVATE -w)
