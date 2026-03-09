# Unit tests for ColorUtils, Despill, Despeckle, DeviceDetection, and InferenceSession

add_executable(test_unit
    test_color_utils.cpp
    test_despill.cpp
    test_despeckle.cpp
    test_device_detection.cpp
    test_inference_session.cpp
)

target_link_libraries(test_unit
    PRIVATE
        corridorkey_core_static
        Catch2::Catch2WithMain
)

# Target-based compiler warnings for tests
if(NOT MSVC)
    target_compile_options(test_unit PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    target_compile_options(test_unit PRIVATE /W4 /WX /permissive-)
endif()

add_test(NAME unit_tests COMMAND test_unit)
set_tests_properties(unit_tests PROPERTIES LABELS "unit")
