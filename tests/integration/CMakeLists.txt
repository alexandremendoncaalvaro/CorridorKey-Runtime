# Integration tests (with files, no GPU)

add_executable(test_integration
    ${PROJECT_SOURCE_DIR}/src/plugins/ofx/ofx_logging.cpp
    ${PROJECT_SOURCE_DIR}/src/plugins/ofx/ofx_runtime_client.cpp
    test_cache_fallback.cpp
    test_doctor_report.cpp
    test_engine_fallback_policy.cpp
    test_engine_mlx.cpp
    test_engine_torch_trt.cpp
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
    test_screen_color_canonicalization.cpp
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

# Tiny helper executable used by the early-child-death regression test for
# issue #56. Spawning a real sidecar that exits immediately on launch is the
# only way to exercise the "process died before binding the port" path
# end-to-end through OfxRuntimeClient::launch_server() and the wait loop in
# ensure_server_running().
add_executable(ofx_runtime_test_quick_exit ofx_runtime_test_quick_exit.cpp)
corridorkey_apply_windows_common_compile_defs(ofx_runtime_test_quick_exit)
if(NOT MSVC)
    target_compile_options(ofx_runtime_test_quick_exit PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    target_compile_options(ofx_runtime_test_quick_exit PRIVATE /W4 /WX /permissive-)
endif()

target_compile_definitions(test_integration PRIVATE
    PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
    OFX_PLUGIN_PATH="$<TARGET_FILE:corridorkey_ofx>"
    OFX_RUNTIME_TEST_QUICK_EXIT_PATH="$<TARGET_FILE:ofx_runtime_test_quick_exit>"
)

# test_ofx_plugin_exceptions dlopen's the built OFX bundle at runtime — the
# generator-expression path alone does not imply a build-order edge, so without
# this dependency a targeted "ninja test_integration" leaves the plugin unbuilt
# and the test fails with a nullptr handle.
add_dependencies(test_integration corridorkey_ofx ofx_runtime_test_quick_exit)

# corridorkey_core carries -fsanitize=address under ENABLE_ASAN while
# test_integration itself does not. When the test dlopen's CorridorKey.ofx the
# ASAN runtime is pulled in after process start and aborts with "Interceptors
# are not working". Surface that at compile time so the test can skip cleanly
# under sanitized presets instead of SIGABRT-ing.
if(ENABLE_ASAN)
    target_compile_definitions(test_integration PRIVATE CORRIDORKEY_DEPS_HAVE_ASAN=1)
endif()

if(NOT MSVC)
    target_compile_options(test_integration PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    target_compile_options(test_integration PRIVATE /W4 /WX /permissive-)
endif()

add_test(NAME integration_tests COMMAND test_integration)
set_tests_properties(integration_tests PROPERTIES LABELS "integration")

if(CORRIDORKEY_HAS_TORCHTRT AND TARGET corridorkey-torchtrt-runner)
    add_test(
        NAME torchtrt_dynamic_runner_smoke
        COMMAND
            ${CMAKE_COMMAND}
            -DRUNNER_EXE=$<TARGET_FILE:corridorkey-torchtrt-runner>
            -DTS_PATH=${PROJECT_SOURCE_DIR}/temp/dynamic-rtx/corridorkey_dynamic_blue_fp16.ts
            -DBIN_DIR=${CORRIDORKEY_TORCHTRT_RUNTIME_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_optional_torchtrt_runner.cmake
    )
    set_tests_properties(torchtrt_dynamic_runner_smoke PROPERTIES
        LABELS "integration;torchtrt;dynamic;hardware"
        SKIP_REGULAR_EXPRESSION "^SKIP:"
        TIMEOUT 300
    )
endif()

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

if(CORRIDORKEY_HAS_CUDA)
    target_compile_definitions(ofx_benchmark_harness PRIVATE CORRIDORKEY_HAS_CUDA=1)
    target_link_libraries(ofx_benchmark_harness PRIVATE CUDA::cudart_static)
endif()

corridorkey_stage_ort_runtime(ofx_benchmark_harness)

if(NOT MSVC)
    target_compile_options(ofx_benchmark_harness PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    target_compile_options(ofx_benchmark_harness PRIVATE /W4 /WX /permissive-)
endif()

# RPC-path harness: spawns the real runtime-server binary via OfxRuntimeClient
# and measures per-frame latency end-to-end. Used to isolate IPC-layer
# regressions from broker/engine regressions.
add_executable(ofx_rpc_benchmark_harness
    ${PROJECT_SOURCE_DIR}/src/plugins/ofx/ofx_logging.cpp
    ${PROJECT_SOURCE_DIR}/src/plugins/ofx/ofx_runtime_client.cpp
    ofx_rpc_benchmark_harness.cpp
)

corridorkey_apply_windows_common_compile_defs(ofx_rpc_benchmark_harness)

target_link_libraries(ofx_rpc_benchmark_harness
    PRIVATE
        corridorkey_core_static
        nlohmann_json::nlohmann_json
)

target_include_directories(ofx_rpc_benchmark_harness PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/vendor/openfx/include)

target_compile_definitions(ofx_rpc_benchmark_harness PRIVATE
    PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

corridorkey_stage_ort_runtime(ofx_rpc_benchmark_harness)

if(NOT MSVC)
    target_compile_options(ofx_rpc_benchmark_harness PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    target_compile_options(ofx_rpc_benchmark_harness PRIVATE /W4 /WX /permissive-)
endif()

# Regression guard for the v0.7.6-mac.1 "scrubbing absurdly slow" bug caused
# by posix_spawn inheriting Resolve's low parent QoS into the runtime server.
# The harness invocation below spawns the server from a BACKGROUND-QoS
# parent; the test asserts steady-state latency recovers. Without the fix,
# per-frame latency held at 30-120s.
if(APPLE)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME macos_runtime_qos_regression
            COMMAND
                ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/test_macos_runtime_qos_regression.py
        )
        set_tests_properties(macos_runtime_qos_regression PROPERTIES
            LABELS "integration;macos;regression;slow"
            TIMEOUT 600
            ENVIRONMENT
                "HARNESS_BIN=$<TARGET_FILE:ofx_rpc_benchmark_harness>;SERVER_BIN=$<TARGET_FILE:corridorkey_cli>;PROJECT_ROOT=${PROJECT_SOURCE_DIR}"
        )
    endif()
endif()
