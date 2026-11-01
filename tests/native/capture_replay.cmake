# Capture/replay equivalence: run the synthetic frame workload with
# native_capture, then replay the capture without the title and require the
# same scanout, GPU-written memory and sample counters in every pass.
#   -DPROBE=... -DREPLAY=... -DOUTPUT=... -DAPI=d3d12|vulkan
file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")
execute_process(COMMAND "${PROBE}" "${OUTPUT}/run" ${API} 8
    native_capture=${OUTPUT}/frames.x360nfc native_capture_skip=3 native_capture_frames=4
    native_frame_profile=${OUTPUT}/live-frames.csv
    RESULT_VARIABLE probe_result OUTPUT_VARIABLE probe_output ERROR_VARIABLE probe_error TIMEOUT 240)
message("${probe_output}${probe_error}")
if(NOT probe_result EQUAL 0)
  message(FATAL_ERROR "frame workload failed: ${probe_result}")
endif()
if(NOT EXISTS "${OUTPUT}/frames.x360nfc")
  message(FATAL_ERROR "capture was not written")
endif()
file(STRINGS "${OUTPUT}/live-frames.csv" profile_rows)
list(LENGTH profile_rows profile_count)
if(profile_count LESS 9)
  message(FATAL_ERROR "frame profile has ${profile_count} rows, expected a header and 8 frames")
endif()
execute_process(COMMAND "${REPLAY}" "${OUTPUT}/frames.x360nfc" "${OUTPUT}/replay" --backend ${API} --repeat 3 --stats
    RESULT_VARIABLE replay_result OUTPUT_VARIABLE replay_output ERROR_VARIABLE replay_error TIMEOUT 240)
message("${replay_output}${replay_error}")
if(NOT replay_result EQUAL 0)
  message(FATAL_ERROR "replay differs from the capture or failed: ${replay_result}")
endif()
string(FIND "${replay_output}" "\"frames\":12" frames_found)
string(FIND "${replay_output}" "\"frontbuffer_checks\":12" checks_found)
if(frames_found EQUAL -1 OR checks_found EQUAL -1)
  message(FATAL_ERROR "replay did not check 4 frames in 3 passes")
endif()
# Negative control: the same replay without restoring EDRAM must be detected.
execute_process(COMMAND "${REPLAY}" "${OUTPUT}/frames.x360nfc" "${OUTPUT}/replay-no-edram" --backend ${API} --skip-edram
    RESULT_VARIABLE control_result OUTPUT_VARIABLE control_output ERROR_VARIABLE control_error TIMEOUT 240)
message("${control_output}${control_error}")
if(NOT control_result EQUAL 3)
  message(FATAL_ERROR "replay without EDRAM was not reported as different (exit ${control_result})")
endif()
