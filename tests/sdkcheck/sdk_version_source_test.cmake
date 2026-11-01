# The SDK resolves its version from its own repository, never from the
# consumer that adds it as a subdirectory. A consumer with a stable tag above
# the SDK floor (x360-native v1.28.8) aborted every SDK configure.
#
# cmake -DSDK_DIR=<external/rexglue-sdk> -DWORK_DIR=<scratch> -P sdk_version_source_test.cmake
cmake_minimum_required(VERSION 3.25)
foreach(required SDK_DIR WORK_DIR)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
find_program(GIT_EXECUTABLE git REQUIRED)

file(READ "${SDK_DIR}/CMakeLists.txt" sdk_lists)
if(NOT sdk_lists MATCHES "project\\(ReXGlue[ \t\r\n]+VERSION ([0-9]+)\\.([0-9]+)\\.([0-9]+)")
  message(FATAL_ERROR "SDK project version not found")
endif()
set(PROJECT_VERSION_MAJOR ${CMAKE_MATCH_1})
set(PROJECT_VERSION_MINOR ${CMAKE_MATCH_2})
set(sdk_version "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
if(NOT sdk_lists MATCHES "(rex_resolve_version\\(REXGLUE_FULL_VERSION[^)]*\\))")
  message(FATAL_ERROR "SDK version resolution call not found")
endif()
set(resolve_call "${CMAKE_MATCH_1}")
include("${SDK_DIR}/cmake/rex_version.cmake")
math(EXPR consumer_major "${PROJECT_VERSION_MAJOR} + 99")

function(git directory)
  execute_process(COMMAND "${GIT_EXECUTABLE}" -c user.name=x360n -c user.email=x360n@invalid
                          -c commit.gpgsign=false -c tag.gpgsign=false ${ARGN}
                  WORKING_DIRECTORY "${directory}" RESULT_VARIABLE rc OUTPUT_QUIET ERROR_QUIET)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "git ${ARGN} failed in ${directory}")
  endif()
endfunction()

function(repository directory)
  file(MAKE_DIRECTORY "${directory}")
  git("${directory}" init -q)
  git("${directory}" commit -q --allow-empty -m init)
  foreach(tag IN LISTS ARGN)
    git("${directory}" tag "${tag}")
  endforeach()
endfunction()

# The SDK call as written in its CMakeLists.txt, from a consumer subdirectory.
function(resolve consumer sdk out_var)
  set(CMAKE_SOURCE_DIR "${consumer}")
  set(CMAKE_CURRENT_SOURCE_DIR "${sdk}")
  cmake_language(EVAL CODE "${resolve_call}")
  set(${out_var} "${REXGLUE_FULL_VERSION}" PARENT_SCOPE)
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
set(consumer "${WORK_DIR}/consumer")
repository("${consumer}" "v${consumer_major}.0.0")
git("${consumer}" commit -q --allow-empty -m "after the release")

# A full SDK clone reports its own exact release tag.
repository("${consumer}/tagged-sdk" "v${sdk_version}")
resolve("${consumer}" "${consumer}/tagged-sdk" version)
if(NOT version STREQUAL sdk_version)
  message(FATAL_ERROR "tagged SDK resolved '${version}', expected '${sdk_version}'")
endif()

# Without SDK tags the floor fallback applies; the consumer tag is still ignored.
repository("${consumer}/untagged-sdk")
resolve("${consumer}" "${consumer}/untagged-sdk" version)
set(fallback "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.0.0-dev.unknown")
if(NOT version STREQUAL fallback)
  message(FATAL_ERROR "untagged SDK resolved '${version}', expected '${fallback}'")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
message(STATUS "SDK version comes from its own repository: ${sdk_version}")
