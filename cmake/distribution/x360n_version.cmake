# Application version from the Conventional Commits history
# (scripts/packaging/compute-version.ps1). Packages pass X360_VERSION explicitly;
# a development tree computes it at configure time and marks it -dev, since later
# commits do not reconfigure. Sets X360N_VERSION and its numeric parts.
set(X360_VERSION "" CACHE STRING "Application version (MAJOR.MINOR.PATCH[-prerelease]); empty computes a -dev version")
if(X360_VERSION)
  set(X360N_VERSION "${X360_VERSION}")
elseif(X360_DISTRIBUTION)
  message(FATAL_ERROR "Distribution requires X360_VERSION")
else()
  set(_x360_version "0.0.0")
  find_program(X360N_POWERSHELL NAMES powershell pwsh)
  if(X360N_POWERSHELL)
    execute_process(COMMAND "${X360N_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass -File
        "${PROJECT_SOURCE_DIR}/scripts/packaging/compute-version.ps1" -Repository "${PROJECT_SOURCE_DIR}"
      OUTPUT_VARIABLE _x360_computed OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _x360_result ERROR_QUIET)
    if(_x360_result EQUAL 0 AND _x360_computed MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
      set(_x360_version "${_x360_computed}")
    endif()
  endif()
  set(X360N_VERSION "${_x360_version}-dev")
endif()
if(NOT X360N_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-[0-9A-Za-z.-]+)?$")
  message(FATAL_ERROR "Invalid X360_VERSION: ${X360N_VERSION}")
endif()
set(X360N_VERSION_MAJOR ${CMAKE_MATCH_1})
set(X360N_VERSION_MINOR ${CMAKE_MATCH_2})
set(X360N_VERSION_PATCH ${CMAKE_MATCH_3})
message(STATUS "x360-native version: ${X360N_VERSION}")
