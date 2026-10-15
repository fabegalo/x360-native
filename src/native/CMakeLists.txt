# Share shader semantics and packet/register services, never the Xenos executor.
set(_gpu ${X360_REXGLUE_DIR}/src/graphics)
add_library(rexgpu-native SHARED plugin.cpp command_processor.cpp packet_guard.cpp d3d12_scanout.cpp texture.cpp
    ${_gpu}/graphics_system.cpp ${_gpu}/command_processor.cpp
    ${_gpu}/register_file.cpp ${_gpu}/registers.cpp ${_gpu}/flags.cpp
    shader_cache.cpp d3d12_render.cpp d3d12_draw.cpp rectangle_shader.cpp point_shader.cpp d3d12_resolve.cpp d3d12_textures.cpp edram_format.cpp ${_gpu}/util/draw.cpp
    ${_gpu}/pipeline/shader/shader.cpp ${_gpu}/format/ucode.cpp
    ${_gpu}/pipeline/shader/translator.cpp ${_gpu}/pipeline/shader/translator_disasm.cpp
    ${_gpu}/pipeline/shader/dxbc.cpp ${_gpu}/pipeline/shader/dxbc_translator.cpp
    ${_gpu}/pipeline/shader/dxbc_translator_alu.cpp ${_gpu}/pipeline/shader/dxbc_translator_fetch.cpp
    ${_gpu}/pipeline/shader/dxbc_translator_memexport.cpp ${_gpu}/pipeline/shader/dxbc_translator_om.cpp
    ${X360_REXGLUE_DIR}/thirdparty/dxbc/DXBCChecksum.cpp)
target_link_libraries(rexgpu-native PRIVATE rexruntime xxHash::xxhash d3d12 dxgi dxguid d3dcompiler)
target_include_directories(rexgpu-native PRIVATE ${X360_REXGLUE_DIR})
set_target_properties(rexgpu-native PROPERTIES DEBUG_POSTFIX d RELWITHDEBINFO_POSTFIX rd)
target_compile_options(rexgpu-native PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_compile_definitions(rexgpu-native PRIVATE REX_GPU_VALIDATE_PACKET=1)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/scanout.hlsl NATIVE_SCANOUT_HLSL)
configure_file(scanout_source.h.in scanout_source.h @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS scanout.hlsl)
target_include_directories(rexgpu-native PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(rexgpu-native PRIVATE ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
if(X360N_NATIVE_PLUGIN_ONLY)
  return()
endif()
add_executable(x360n_native_probe ../../tests/native/probe.cpp)
target_link_libraries(x360n_native_probe PRIVATE rexruntime windowscodecs ole32)
x360n_stage_runtime_dlls(x360n_native_probe PLUGIN_TARGETS rexgpu-native rexgpu-xenos)
if(TARGET Catch2::Catch2WithMain)
  add_executable(x360n_native_tests ../../tests/native/texture_test.cpp ../../tests/native/packet_test.cpp ../../tests/native/rectangle_test.cpp ../../tests/native/point_test.cpp texture.cpp packet_guard.cpp rectangle_shader.cpp point_shader.cpp ${_gpu}/util/draw.cpp ${_gpu}/register_file.cpp ${_gpu}/registers.cpp ${_gpu}/flags.cpp ${X360_REXGLUE_DIR}/thirdparty/dxbc/DXBCChecksum.cpp)
  target_include_directories(x360n_native_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${X360_REXGLUE_DIR} ${X360_REXGLUE_DIR}/thirdparty/renderdoc)
  target_link_libraries(x360n_native_tests PRIVATE rexruntime Catch2::Catch2WithMain d3d12 d3dcompiler)
  x360n_stage_runtime_dlls(x360n_native_tests)
  add_test(NAME native.texture COMMAND x360n_native_tests ~[native-packet])
  set_tests_properties(native.texture PROPERTIES LABELS project TIMEOUT 30)
  add_test(NAME native.packet-payload COMMAND x360n_native_tests [native-packet])
  set_tests_properties(native.packet-payload PROPERTIES LABELS project TIMEOUT 30)
  add_test(NAME native.abi COMMAND x360n_native_probe native ${CMAKE_CURRENT_BINARY_DIR}/abi-probe --abi)
  set_tests_properties(native.abi PROPERTIES LABELS project TIMEOUT 20)
  add_test(NAME sdkcheck.Native-packet-guard COMMAND ${CMAKE_COMMAND}
      -DPROBE=$<TARGET_FILE:x360n_native_probe> -DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/guard-probe
      -P ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/native/reject_packet.cmake)
  set_tests_properties(sdkcheck.Native-packet-guard PROPERTIES LABELS project TIMEOUT 25)
endif()
