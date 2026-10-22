# Native EDRAM barriers follow the ROV layout: --draw-interleaved records
# 1 + 4 x 32 ROV draws of a retained draw, a resolve that clears the color
# target, and a color base moved over half of the same EDRAM range and back.
# Only the two layout changes need their own barrier; the resolve places three
# (before its copy, before and after its clear) and the final resolve one.
# The capture must match Xenos/D3D12 for the same sequence.
foreach(var PROBE OUTPUT SNAPSHOT)
  if(NOT DEFINED ${var})
    message(FATAL_ERROR "${var} is required")
  endif()
endforeach()
foreach(backend native xenos:d3d12)
  string(REPLACE ":" "-" dir "${backend}")
  file(REMOVE_RECURSE "${OUTPUT}/${dir}")
  execute_process(COMMAND "${PROBE}" ${backend} "${OUTPUT}/${dir}" --draw-interleaved "${SNAPSHOT}"
      RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${backend} interleaved replay failed (${result}):\n${out}\n${err}")
  endif()
endforeach()
file(STRINGS "${OUTPUT}/native/runtime.log" barriers REGEX "Native EDRAM barriers")
if(NOT barriers MATCHES "edram_barriers=6 edram_layout_barriers=2 draws=129")
  message(FATAL_ERROR "unexpected Native EDRAM barriers: '${barriers}'")
endif()
file(SHA256 "${OUTPUT}/native/draw.png" native_hash)
file(SHA256 "${OUTPUT}/xenos-d3d12/draw.png" xenos_hash)
if(NOT native_hash STREQUAL xenos_hash)
  message(FATAL_ERROR "Native capture ${native_hash} differs from Xenos/D3D12 ${xenos_hash}")
endif()
message(STATUS "PASS ${barriers}; capture ${native_hash} matches Xenos/D3D12")
