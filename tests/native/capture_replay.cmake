# Capture/replay equivalence: run the synthetic frame workload with
# native_capture, then replay the capture without the title and require the
# same scanout, GPU-written memory, sample counters and presenter output
# (gamma LUT and post effect composed by the real presenter) in every pass,
# with identical results across passes. Negative controls must be detected.
#   -DPROBE=... -DREPLAY=... -DOUTPUT=... -DAPI=d3d12|vulkan
#   Extra cvars for both programs (e.g. d3d12_adapter=-2 for WARP on CI):
#   environment X360N_NATIVE_TEST_CVARS, separated by ';'.
file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")
set(extra "$ENV{X360N_NATIVE_TEST_CVARS}")
function(run expected name)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 300)
  message("[${name}] exit ${result}\n${out}${err}")
  if(NOT result EQUAL expected)
    message(FATAL_ERROR "${name}: exit ${result}, expected ${expected}")
  endif()
  set(last_output "${out}" PARENT_SCOPE)
endfunction()
foreach(effect none fxaa)
  set(dir "${OUTPUT}/${effect}")
  run(0 "workload ${effect}" "${PROBE}" "${dir}/run" ${API} 8
      native_capture=${dir}/frames.x360nfc native_capture_skip=3 native_capture_frames=4
      native_frame_profile=${dir}/live-frames.csv swap_post_effect=${effect} ${extra})
  file(STRINGS "${dir}/live-frames.csv" rows)
  list(LENGTH rows count)
  if(count LESS 9)
    message(FATAL_ERROR "frame profile has ${count} rows, expected a header and 8 frames")
  endif()
  run(0 "replay ${effect}" "${REPLAY}" "${dir}/frames.x360nfc" "${dir}/replay" --backend ${API} --repeat 3 --stats ${extra})
  foreach(expected "\"frames\":12" "\"frontbuffer_checks\":12" "\"output_checks\":12" "\"repeatable\":true" "\"equivalent\":true")
    string(FIND "${last_output}" "${expected}" found)
    if(found EQUAL -1)
      message(FATAL_ERROR "replay ${effect}: ${expected} missing")
    endif()
  endforeach()
endforeach()
set(capture "${OUTPUT}/none/frames.x360nfc")
# Negative controls: missing EDRAM state and one altered input byte must be
# reported as different frames; a damaged file must be refused before replay.
run(3 "control without EDRAM" "${REPLAY}" "${capture}" "${OUTPUT}/no-edram" --backend ${API} --skip-edram ${extra})
run(3 "control with altered vertices" "${REPLAY}" "${capture}" "${OUTPUT}/tampered" --backend ${API} --tamper-memory 0 ${extra})
file(SIZE "${capture}" size)
math(EXPR truncated "${size} - 7")
file(COPY_FILE "${capture}" "${OUTPUT}/truncated.x360nfc")
run(0 "truncate copy" "${PROBE}" --truncate "${OUTPUT}/truncated.x360nfc" ${truncated})
run(2 "control with a truncated file" "${REPLAY}" "${OUTPUT}/truncated.x360nfc" "${OUTPUT}/truncated" --backend ${API} ${extra})
