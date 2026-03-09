# Unit tests for ColorUtils and other stateless logic

add_executable(test_unit 
    test_color_utils.cpp
    test_inference_session.cpp
)

target_link_libraries(test_unit 
    PRIVATE 
        corridorkey_core
        Catch2::Catch2WithMain
)

add_test(NAME unit_tests COMMAND test_unit)
set_tests_properties(unit_tests PROPERTIES LABELS "unit")
