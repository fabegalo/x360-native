# Headless GPU integration, no original titles or user input. Fails without 0074.
execute_process(COMMAND "${PROBE}" native "${OUTPUT}" --reject
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 20)
if(NOT result EQUAL 78 OR NOT error MATCHES "PM4 opcode 0x7E is not supported by Native")
  message(FATAL_ERROR "Native retired an unsupported command or failed for another reason: ${result}\n${output}\n${error}")
endif()
