# Included from tests/CMakeLists.txt; paths and target outputs stay rooted there.
add_test(NAME env.doctor COMMAND x360n doctor --config $<CONFIG>)
set_tests_properties(env.doctor PROPERTIES LABELS "project" TIMEOUT 300)

# Functional reference (docs/reports/titles/validation-iruka.md): runs the recompiled demo
# title built from titles/iruka and checks levels L1-L5. Needs the title build
# (cmake --preset x360n-win-amd64 in titles/iruka) and the demo assets in
# .local/inputs; skipped (77) otherwise. Opens a window, so it runs alone.
add_test(NAME local.iruka.regression
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${PROJECT_SOURCE_DIR}/scripts/validation/regress-iruka.ps1
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(local.iruka.regression PROPERTIES
    LABELS "project" TIMEOUT 180 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)

# Avatar service and 17559 Guide sharing one original XAM image in Motocross
# Madness (docs/avatar-system.md §17). Needs both prepared builds and the user's
# originals; skipped (77) otherwise. Opens a window, so it runs alone.
add_test(NAME local.motocross.shared-xam
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File ${PROJECT_SOURCE_DIR}/tests/local/motocross-shared-xam.ps1
        -Supervisor $<TARGET_FILE:x360n>
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
set_tests_properties(local.motocross.shared-xam PROPERTIES
    LABELS "project" TIMEOUT 180 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)

