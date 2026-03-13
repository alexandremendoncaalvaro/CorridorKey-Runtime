# Integration tests (with files, no GPU)

add_executable(test_integration
    test_cache_fallback.cpp
    test_doctor_report.cpp
    test_engine_mlx.cpp
    test_engine_tiling.cpp
    test_exr_roundtrip.cpp
    test_png_roundtrip.cpp
    test_video_roundtrip.cpp
    test_post_process_chain.cpp
    test_engine_warmup.cpp
    test_regression_0000_video_decode_bframes.cpp
)

target_link_libraries(test_integration
    PRIVATE
        corridorkey_core_static
        Catch2::Catch2WithMain
)

target_compile_definitions(test_integration PRIVATE PROJECT_ROOT="${CMAKE_SOURCE_DIR}")

if(NOT MSVC)
    target_compile_options(test_integration PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    target_compile_options(test_integration PRIVATE /W4 /WX /permissive-)
endif()

add_test(NAME integration_tests COMMAND test_integration)
set_tests_properties(integration_tests PROPERTIES LABELS "integration")
