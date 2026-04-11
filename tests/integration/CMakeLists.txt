# Integration tests (with files, no GPU)

add_executable(test_integration
    ${PROJECT_SOURCE_DIR}/src/plugins/ofx/ofx_logging.cpp
    ${PROJECT_SOURCE_DIR}/src/plugins/ofx/ofx_runtime_client.cpp
    test_cache_fallback.cpp
    test_doctor_report.cpp
    test_engine_fallback_policy.cpp
    test_engine_mlx.cpp
    test_engine_tiling.cpp
    test_exr_roundtrip.cpp
    test_ofx_runtime_client.cpp
    test_ofx_runtime_service.cpp
    test_ofx_session_broker.cpp
    test_ofx_plugin_exceptions.cpp
    test_job_orchestrator.cpp
    test_png_roundtrip.cpp
    test_video_roundtrip.cpp
    test_post_process_chain.cpp
    test_shared_memory_transport.cpp
    test_windows_gpu_detection.cpp
    test_engine_warmup.cpp
    test_regression_0000_video_decode_bframes.cpp
)

corridorkey_apply_windows_common_compile_defs(test_integration)

target_link_libraries(test_integration
    PRIVATE
        corridorkey_core_static
        Catch2::Catch2WithMain
)

target_include_directories(test_integration PRIVATE ${PROJECT_SOURCE_DIR}/vendor/openfx/include)

corridorkey_stage_ort_runtime(test_integration)

target_compile_definitions(test_integration PRIVATE
    PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
    OFX_PLUGIN_PATH="$<TARGET_FILE:corridorkey_ofx>"
)

if(NOT MSVC)
    target_compile_options(test_integration PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    target_compile_options(test_integration PRIVATE /W4 /WX /permissive-)
endif()

add_test(NAME integration_tests COMMAND test_integration)
set_tests_properties(integration_tests PROPERTIES LABELS "integration")

add_executable(ofx_benchmark_harness
    ofx_benchmark_harness.cpp
)

corridorkey_apply_windows_common_compile_defs(ofx_benchmark_harness)

target_link_libraries(ofx_benchmark_harness
    PRIVATE
        corridorkey_core_static
        nlohmann_json::nlohmann_json
)

target_include_directories(ofx_benchmark_harness PRIVATE ${PROJECT_SOURCE_DIR}/vendor/openfx/include)

target_compile_definitions(ofx_benchmark_harness PRIVATE
    PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

corridorkey_stage_ort_runtime(ofx_benchmark_harness)

if(NOT MSVC)
    target_compile_options(ofx_benchmark_harness PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    target_compile_options(ofx_benchmark_harness PRIVATE /W4 /WX /permissive-)
endif()
