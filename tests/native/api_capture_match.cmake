# Patch 0143: Xenos/Vulkan applies the texture fetch exp_adjust (word 3) like
# Xenos/D3D12. The retained draw samples k_2_10_10_10_AS_16_16_16_16 textures
# with exp_adjust +2; without the patch the Vulkan capture is 4x darker.
foreach(var PROBE OUTPUT SNAPSHOT)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "${var} is required")
  endif()
endforeach()
foreach(api d3d12 vulkan)
  file(REMOVE_RECURSE "${OUTPUT}/${api}")
  execute_process(COMMAND "${PROBE}" xenos:${api} "${OUTPUT}/${api}" --draw-replay "${SNAPSHOT}"
      RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "xenos:${api} draw replay failed (${result}):\n${out}\n${err}")
  endif()
  file(SHA256 "${OUTPUT}/${api}/draw.png" hash_${api})
endforeach()
if(NOT hash_vulkan STREQUAL hash_d3d12)
  message(FATAL_ERROR "Vulkan capture ${hash_vulkan} differs from D3D12 ${hash_d3d12}")
endif()
message(STATUS "PASS Vulkan and D3D12 captures match: ${hash_vulkan}")
