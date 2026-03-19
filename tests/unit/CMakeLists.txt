# Unit tests for ColorUtils, Despill, Despeckle, DeviceDetection, and InferenceSession

add_executable(test_unit
    test_color_utils.cpp
    test_despill.cpp
    test_despeckle.cpp
    test_device_selection.cpp
    test_device_detection.cpp
    test_inference_session.cpp
    test_ofx_model_selection.cpp
    test_output_path_utils.cpp
    test_process_paths.cpp
    test_runtime_contracts.cpp
    test_session_cache_policy.cpp
    test_session_policy.cpp
    test_source_passthrough.cpp
    test_stage_profiler.cpp
)

target_link_libraries(test_unit
    PRIVATE
        corridorkey_core_static
        Catch2::Catch2WithMain
)

target_include_directories(test_unit
    PRIVATE
        ${PROJECT_SOURCE_DIR}/vendor/openfx/include
)

corridorkey_stage_ort_runtime(test_unit)

# Target-based compiler warnings for tests
if(NOT MSVC)
    target_compile_options(test_unit PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    target_compile_options(test_unit PRIVATE /W4 /WX /permissive-)
endif()

add_test(NAME unit_tests COMMAND test_unit)
set_tests_properties(unit_tests PROPERTIES LABELS "unit")
