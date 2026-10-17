# Test groups (ctest name prefixes):
#   x360n.*      supervisor unit/process tests; no proprietary content
#   x360n.ppc.*  our own PowerPC program through the SDK recompiler
#   sdkcheck.*   checks of SDK behavior we depend on or diagnosed
#   env.*        depends on this machine's toolchain (doctor)
#   local.*      needs user-supplied files in .local/inputs; SKIPPED when absent
# All of the above carry the single label "project" (ctest -L project); filter a
# group by name (ctest -R "local[.]"; devshell.cmd eats "^"). One label only: catch_discover_tests
# flattens a multi-value LABELS list into PROPERTIES, which shifts the
# key/value pairs and silently drops TIMEOUT and SKIP_RETURN_CODE.
# The SDK's own suites keep their prefixes: unit.* and ppc.*.
list(APPEND CMAKE_MODULE_PATH "${X360_REXGLUE_DIR}/thirdparty/catch2/extras")
include(Catch)

include(cmake/graphics.cmake)
include(cmake/project.cmake)
include(cmake/local.cmake)
include(cmake/sdk.cmake)
include(cmake/sessions.cmake)
include(cmake/contracts.cmake)
