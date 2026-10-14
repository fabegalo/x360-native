# Headless GPU integration, no original titles or user input. Fails without 0074.
execute_process(COMMAND "${PROBE}" native "${OUTPUT}" --reject
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20)
if(NOT result EQUAL 78 OR NOT error MATCHES "PM4 opcode 0x7E is not supported by Native")
  message(FATAL_ERROR "Native retired an unsupported command or failed for another reason: ${result}\n${output}\n${error}")
endif()
# Payload checks must run through the plugin and the SDK hook (0075), before
# the shared decoder can read beyond inline data or silently drop a draw.
foreach(mode IN ITEMS inline draw)
  execute_process(COMMAND "${PROBE}" native "${OUTPUT}-${mode}" "--reject-${mode}"
      RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20)
  if(mode STREQUAL "inline")
    set(expected "inline shader exceeds contiguous packet extent")
  else()
    set(expected "immediate index source not implemented")
  endif()
  if(NOT result EQUAL 78 OR NOT error MATCHES "${expected}")
    message(FATAL_ERROR "Native payload gate failed (${mode}): ${result}\n${output}\n${error}")
  endif()
endforeach()
