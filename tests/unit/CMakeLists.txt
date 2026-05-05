# Unit tests for ColorUtils, Despill, Despeckle, DeviceDetection, and InferenceSession

add_executable(test_unit
    ${PROJECT_SOURCE_DIR}/src/plugins/ofx/ofx_actions.cpp
    ${PROJECT_SOURCE_DIR}/src/plugins/ofx/ofx_frame_cache.cpp
    ${PROJECT_SOURCE_DIR}/src/plugins/ofx/ofx_image_utils.cpp
    ${PROJECT_SOURCE_DIR}/src/plugins/ofx/ofx_instance.cpp
    ${PROJECT_SOURCE_DIR}/src/plugins/ofx/ofx_logging.cpp
    ${PROJECT_SOURCE_DIR}/src/plugins/ofx/ofx_render.cpp
    ${PROJECT_SOURCE_DIR}/src/plugins/ofx/ofx_runtime_client.cpp
    test_alpha_edge.cpp
    test_color_utils.cpp
    test_coarse_to_fine_policy.cpp
    test_despill.cpp
    test_despeckle.cpp
    test_device_selection.cpp
    test_execution_defaults.cpp
    test_device_detection.cpp
    test_inference_session.cpp
    test_inference_postprocess_policy.cpp
    test_job_orchestrator.cpp
    test_json_utils.cpp
    test_local_ipc.cpp
    test_model_artifacts.cpp
    test_model_input_normalization.cpp
    test_ort_process_context.cpp
    test_ofx_color_management.cpp
    test_ofx_frame_cache.cpp
    test_ofx_help_routing.cpp
    test_ofx_host_detection.cpp
    test_ofx_image_utils.cpp
    test_ofx_backend_matching.cpp
    test_ofx_lifecycle.cpp
    test_ofx_model_selection.cpp
    test_ofx_output_modes.cpp
    test_ofx_screen_color.cpp
    test_ofx_runtime_client.cpp
    test_ofx_runtime_panel.cpp
    test_ofx_runtime_protocol.cpp
    test_ofx_session_policy.cpp
    test_output_path_utils.cpp
    test_process_paths.cpp
    test_runtime_diagnostics.cpp
    test_runtime_contracts.cpp
    test_session_cache_policy.cpp
    test_session_policy.cpp
    test_source_passthrough.cpp
    test_fp16_convert.cpp
    test_pinned_buffer.cpp
    test_stage_profiler.cpp
    test_ofx_stubs.cpp
    test_tile_blend.cpp
    test_version_check.cpp
    test_windows_rtx_probe.cpp
    test_warmup_policy.cpp
    test_gpu_prep.cpp
    test_gpu_resize.cpp
)

corridorkey_apply_windows_common_compile_defs(test_unit)

target_link_libraries(test_unit
    PRIVATE
        corridorkey_core_static
        Catch2::Catch2WithMain
)

target_include_directories(test_unit
    PRIVATE
        ${PROJECT_SOURCE_DIR}/vendor/openfx/include
)

target_compile_definitions(test_unit PRIVATE
    PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

corridorkey_stage_ort_runtime(test_unit)

# Target-based compiler warnings for tests
if(NOT MSVC)
    target_compile_options(test_unit PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    target_compile_options(test_unit PRIVATE /W4 /WX /permissive-)
endif()

# Process isolation between ORT/TensorRT-touching tests and raw CUDA
# helper tests. Tests that exercise the inference engine through ORT/TRT
# leave the CUDA driver state in conditions that can SIGSEGV a subsequent
# raw cudaStreamCreate inside this process. The leak is documented in
# ONNX Runtime issue #18466 (cublas/cudnn handles created in TRT EP
# constructor without matching destroy calls) and acknowledged in the
# ONNX Runtime CUDA Programming wiki ("CUDA code sometimes hangs in
# destruction"). Catch2 does not support per-TEST_CASE process isolation
# natively (catchorg/Catch2 issue #462), so we split the suite into two
# ctest entries that each spawn a fresh process and therefore see a clean
# CUDA primary context. The same test_unit binary is reused -- the only
# difference is the Catch2 tag filter passed as argv[1].
add_test(NAME unit_tests COMMAND test_unit "~[core]")
add_test(NAME unit_tests_gpu COMMAND test_unit "[core]")

set_tests_properties(unit_tests unit_tests_gpu PROPERTIES
    LABELS "unit"
    ENVIRONMENT "CUDA_LAUNCH_BLOCKING=1"
)
