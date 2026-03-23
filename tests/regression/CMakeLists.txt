# Regression tests for specific bug fixes

add_executable(test_regression
    test_placeholder.cpp
    test_regression_dml_provider.cpp
    test_regression_hardware_providers.cpp
)

target_link_libraries(test_regression
    PRIVATE
        corridorkey_core_static
        Catch2::Catch2WithMain
)

corridorkey_stage_ort_runtime(test_regression)

if(NOT MSVC)
    target_compile_options(test_regression PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    target_compile_options(test_regression PRIVATE /W4 /WX /permissive-)
endif()

add_test(NAME regression_tests COMMAND test_regression)
set_tests_properties(regression_tests PROPERTIES LABELS "regression")
