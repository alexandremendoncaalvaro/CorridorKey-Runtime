# Regression tests for specific bug fixes

add_executable(test_regression
    test_placeholder.cpp
    test_regression_0056_bundle_vcruntime.cpp
    test_regression_dml_provider.cpp
    test_regression_hardware_providers.cpp
    test_regression_path_b_quality_loop.cpp
)

target_link_libraries(test_regression
    PRIVATE
        corridorkey_core_static
        Catch2::Catch2WithMain
)

target_include_directories(test_regression
    PRIVATE
        ${PROJECT_SOURCE_DIR}/vendor/openfx/include
)

corridorkey_stage_ort_runtime(test_regression)

# Wire the OFX bundle path into the regression target so
# test_regression_0056_bundle_vcruntime.cpp can verify the staged Win64 dir
# contains the Visual C++ Redistributable DLLs the Issue #56 fix requires.
# Build-order edge: the bundle is staged by corridorkey_ofx's POST_BUILD
# command, so the regression target depends on corridorkey_ofx running
# first before this assertion has anything to inspect.
if(WIN32 AND TARGET corridorkey_ofx)
    add_dependencies(test_regression corridorkey_ofx)
    target_compile_definitions(test_regression PRIVATE
        CORRIDORKEY_OFX_BUNDLE_WIN64_DIR="${CMAKE_BINARY_DIR}/CorridorKey.ofx.bundle/Contents/Win64"
    )
endif()

if(NOT MSVC)
    target_compile_options(test_regression PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    target_compile_options(test_regression PRIVATE /W4 /WX /permissive-)
endif()

add_test(NAME regression_tests COMMAND test_regression)
set_tests_properties(regression_tests PROPERTIES LABELS "regression")

add_test(
    NAME regression_adobe_sdk_resolution
    COMMAND ${CMAKE_COMMAND} -P
            ${CMAKE_CURRENT_SOURCE_DIR}/test_adobe_sdk_resolution.cmake
)
set_tests_properties(regression_adobe_sdk_resolution PROPERTIES LABELS "regression")

add_test(
    NAME regression_adobe_pipl_metadata
    COMMAND ${CMAKE_COMMAND} -P
            ${CMAKE_CURRENT_SOURCE_DIR}/test_adobe_pipl_metadata.cmake
)
set_tests_properties(regression_adobe_pipl_metadata PROPERTIES LABELS "regression")

if(WIN32)
    add_test(
        NAME regression_adobe_cmake_scaffold
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                ${CMAKE_CURRENT_SOURCE_DIR}/test_adobe_cmake_scaffold.ps1
    )
    set_tests_properties(regression_adobe_cmake_scaffold PROPERTIES LABELS "regression")

    add_test(
        NAME regression_adobe_windows_wrapper_args
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                ${CMAKE_CURRENT_SOURCE_DIR}/test_adobe_windows_wrapper_args.ps1
    )
    set_tests_properties(regression_adobe_windows_wrapper_args PROPERTIES LABELS "regression")

    add_test(
        NAME regression_adobe_package_scaffold
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                ${CMAKE_CURRENT_SOURCE_DIR}/test_adobe_package_scaffold.ps1
    )
    set_tests_properties(regression_adobe_package_scaffold PROPERTIES LABELS "regression")

    add_test(
        NAME regression_windows_doctor_missing_model_tolerance
        COMMAND powershell.exe -NoProfile -ExecutionPolicy Bypass -File
                ${CMAKE_CURRENT_SOURCE_DIR}/test_windows_doctor_missing_model_tolerance.ps1
    )
    set_tests_properties(regression_windows_doctor_missing_model_tolerance PROPERTIES LABELS "regression")
endif()
