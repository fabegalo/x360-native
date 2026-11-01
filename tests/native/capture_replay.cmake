# Capture/replay equivalence: run the synthetic frame workload with
# native_capture, then replay the capture without the title and require the
# same scanout, GPU-written memory, sample counters and presenter output
# (gamma LUT and post effect composed by the real presenter) in every pass,
# with identical results across passes. Negative controls must be detected.
#   -DPROBE=... -DREPLAY=... -DOUTPUT=... -DAPI=d3d12|vulkan
#   Extra cvars for both programs (e.g. d3d12_adapter=-2 for WARP on CI):
#   environment X360N_NATIVE_TEST_CVARS, separated by ';'. Extra workload
#   flags (diagnosis, e.g. --no-sample-counts): X360N_NATIVE_TEST_WORKLOAD_FLAGS.
file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")
set(extra "$ENV{X360N_NATIVE_TEST_CVARS}")
set(workload_flags "$ENV{X360N_NATIVE_TEST_WORKLOAD_FLAGS}")
function(run expected name)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 300)
  message("[${name}] exit ${result}\n${out}${err}")
  if(NOT result EQUAL expected)
    # WARP (the software D3D12 adapter of hosted Windows runners) accepts
    # Native's command lists, the debug layer reports nothing, and its first
    # fence never completes once a ROV color draw is submitted. Only that exact
    # evidence is reported as a skip (SKIP_REGULAR_EXPRESSION): the contract
    # needs a hardware D3D12 adapter; every other failure fails.
    if(API STREQUAL "d3d12" AND EXISTS "${OUTPUT}/none/run/runtime.log")
      file(READ "${OUTPUT}/none/run/runtime.log" runtime)
      string(FIND "${runtime}" "Microsoft Basic Render Driver" warp)
      string(FIND "${out}${err}" "render GPU timeout" hang)
      string(FIND "${out}${err}" "fence 0 of" first)
      if(NOT warp EQUAL -1 AND NOT hang EQUAL -1 AND NOT first EQUAL -1)
        message(FATAL_ERROR "${name}: requires a hardware D3D12 adapter: WARP did not complete the first "
                "submission with a ROV color draw (docs/reports/performance/render-core-2026-11-01.md)")
      endif()
    endif()
    # The runtime logs say why (CI keeps only this output on failure).
    file(GLOB_RECURSE logs "${OUTPUT}/*.log")
    foreach(log IN LISTS logs)
      file(STRINGS "${log}" lines)
      list(LENGTH lines count)
      math(EXPR first "${count} - 60")
      if(first LESS 0)
        set(first 0)
      endif()
      list(SUBLIST lines ${first} 60 tail)
      list(JOIN tail "\n" text)
      message("--- ${log} (last lines)\n${text}")
    endforeach()
    message(FATAL_ERROR "${name}: exit ${result}, expected ${expected}")
  endif()
  set(last_output "${out}" PARENT_SCOPE)
endfunction()
foreach(effect none fxaa)
  set(dir "${OUTPUT}/${effect}")
  run(0 "workload ${effect}" "${PROBE}" "${dir}/run" ${API} 8 ${workload_flags}
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
# Early submission (native_eager_submit, native_eager_submit_depth) must not
# change any frame: a heavy capture (groups of draws, each followed by a
# resolve) replays equivalent and repeatable with it off and at its most
# aggressive settings, and those arms must really submit more often.
set(heavy "${OUTPUT}/heavy")
run(0 "workload heavy" "${PROBE}" "${heavy}/run" ${API} 6 --draws 256 --resolves 4 ${workload_flags}
    native_capture=${heavy}/frames.x360nfc native_capture_skip=3 native_capture_frames=3 ${extra})
# Arms: off, at 1 when the queue is idle, and at 1 keeping one batch queued.
foreach(arm "0;0" "1;0" "1;1")
  list(GET arm 0 eager)
  list(GET arm 1 depth)
  set(name "eager-${eager}-depth-${depth}")
  run(0 "replay heavy ${name}" "${REPLAY}" "${heavy}/frames.x360nfc" "${heavy}/${name}"
      --backend ${API} --repeat 3 --stats native_eager_submit=${eager} native_eager_submit_depth=${depth} ${extra})
  file(READ "${heavy}/${name}/replay.json" report)
  foreach(key equivalent repeatable visual_checked)
    string(JSON value GET "${report}" ${key})
    if(NOT value)
      message(FATAL_ERROR "heavy replay ${name}: ${key} is ${value}")
    endif()
  endforeach()
  string(JSON submissions_${eager}_${depth} GET "${report}" per_frame submissions)
endforeach()
message("heavy replay submissions per warm frame: off ${submissions_0_0}, idle ${submissions_1_0}, queued ${submissions_1_1}")
if(NOT submissions_1_0 GREATER submissions_0_0 OR submissions_1_1 LESS submissions_1_0)
  message(FATAL_ERROR "early submission was not exercised (${submissions_0_0}, ${submissions_1_0}, ${submissions_1_1})")
endif()
# D3D12 skips redundant graphics state by default: recording every call must
# produce the same frames (the filter only removes repeated state).
if(API STREQUAL "d3d12")
  run(0 "replay without the state filter" "${REPLAY}" "${heavy}/frames.x360nfc" "${heavy}/no-state-filter"
      --backend ${API} --repeat 2 native_redundant_state_filter=false ${extra})
endif()
