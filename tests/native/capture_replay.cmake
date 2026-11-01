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
  foreach(expected "\"frames\":12" "\"visual_checked\":true" "\"frontbuffer_checks\":12" "\"output_checks\":12" "\"repeatable\":true" "\"equivalent\":true")
    string(FIND "${last_output}" "${expected}" found)
    if(found EQUAL -1)
      message(FATAL_ERROR "replay ${effect}: ${expected} missing")
    endif()
  endforeach()
endforeach()
set(capture "${OUTPUT}/none/frames.x360nfc")
# Negative controls: missing EDRAM state and one altered input byte must be
# reported as different frames, as must a presenter that yields no composed
# image; a damaged file must be refused before replay.
run(3 "control without EDRAM" "${REPLAY}" "${capture}" "${OUTPUT}/no-edram" --backend ${API} --skip-edram ${extra})
run(3 "control without a composed image" "${REPLAY}" "${capture}" "${OUTPUT}/no-output" --backend ${API} --drop-output ${extra})
string(FIND "${last_output}" "presenter returned no composed image" found)
if(found EQUAL -1)
  message(FATAL_ERROR "a replay without the composed image was not reported as such")
endif()
run(3 "control with altered vertices" "${REPLAY}" "${capture}" "${OUTPUT}/tampered" --backend ${API} --tamper-memory 0 ${extra})
file(SIZE "${capture}" size)
math(EXPR truncated "${size} - 7")
file(COPY_FILE "${capture}" "${OUTPUT}/truncated.x360nfc")
run(0 "truncate copy" "${PROBE}" --truncate "${OUTPUT}/truncated.x360nfc" ${truncated})
run(2 "control with a truncated file" "${REPLAY}" "${OUTPUT}/truncated.x360nfc" "${OUTPUT}/truncated" --backend ${API} ${extra})
# Early submission (native_eager_submit) must not change any frame: a heavy
# capture (groups of draws, each followed by a resolve) replays equivalent and
# repeatable with it off and at its most aggressive setting, and the
# aggressive arm must really submit more often (the change is exercised).
set(heavy "${OUTPUT}/heavy")
run(0 "workload heavy" "${PROBE}" "${heavy}/run" ${API} 6 --draws 256 --resolves 4
    native_capture=${heavy}/frames.x360nfc native_capture_skip=3 native_capture_frames=3 ${extra})
foreach(eager 0 1)
  run(0 "replay heavy eager=${eager}" "${REPLAY}" "${heavy}/frames.x360nfc" "${heavy}/eager-${eager}"
      --backend ${API} --repeat 3 --stats native_eager_submit=${eager} ${extra})
  file(READ "${heavy}/eager-${eager}/replay.json" report)
  foreach(key equivalent repeatable)
    string(JSON value GET "${report}" ${key})
    if(NOT value)
      message(FATAL_ERROR "heavy replay eager=${eager}: ${key} is ${value}")
    endif()
  endforeach()
  string(JSON submissions_${eager} GET "${report}" per_frame submissions)
endforeach()
message("heavy replay submissions per warm frame: eager off ${submissions_0}, eager 1 ${submissions_1}")
if(NOT submissions_1 GREATER submissions_0)
  message(FATAL_ERROR "native_eager_submit=1 did not submit more often (${submissions_1} <= ${submissions_0})")
endif()
