# End-to-end tests (full binary, real model)

add_executable(test_e2e
    test_full_pipeline.cpp
    test_cli_flags.cpp
)

corridorkey_apply_windows_common_compile_defs(test_e2e)

target_link_libraries(test_e2e
    PRIVATE
        corridorkey_core_static
        Catch2::Catch2WithMain
)

corridorkey_stage_ort_runtime(test_e2e)

if(NOT MSVC)
    target_compile_options(test_e2e PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    target_compile_options(test_e2e PRIVATE /W4 /WX /permissive-)
endif()

target_compile_definitions(test_e2e PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")
target_compile_definitions(test_e2e PRIVATE CLI_EXECUTABLE_PATH="$<TARGET_FILE:corridorkey_cli>")

add_test(NAME e2e_tests COMMAND test_e2e)
set_tests_properties(e2e_tests PROPERTIES LABELS "e2e")
add_dependencies(test_e2e corridorkey_cli)
