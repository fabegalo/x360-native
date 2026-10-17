# Included from tests/CMakeLists.txt; paths and target outputs stay rooted there.
# Integration tests over user-supplied content in .local/inputs.
add_executable(x360n_local_tests local/inputs_test.cpp local/avatar_pack_test.cpp
    ${PROJECT_SOURCE_DIR}/titles/common/avatar/avatar_metadata.cpp
    ${PROJECT_SOURCE_DIR}/titles/common/avatar/avatar_toc.cpp)
target_link_libraries(x360n_local_tests PRIVATE x360n_core Catch2::Catch2WithMain)
target_sources(x360n_local_tests PRIVATE local/dashboard_assets_test.cpp
    ../titles/common/dashboard_assets.cpp)
target_compile_definitions(x360n_local_tests PRIVATE X360N_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
# Avatar service static initializers checked against the user's xam.xex image.
target_sources(x360n_local_tests PRIVATE local/avatar_static_init_test.cpp)
# Avatar service files of the 17559 dashboard profile handed to Motocross Madness.
target_sources(x360n_local_tests PRIVATE local/avatar_service_profile_test.cpp)
target_compile_definitions(x360n_local_tests PRIVATE X360N_CONTENT_EXE="$<TARGET_FILE:x360n-content>")
add_dependencies(x360n_local_tests x360n-content)
catch_discover_tests(x360n_local_tests TEST_PREFIX "local." PROPERTIES LABELS "project" TIMEOUT 600)
