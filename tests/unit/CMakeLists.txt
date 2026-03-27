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
    test_json_utils.cpp
    test_local_ipc.cpp
    test_ofx_color_management.cpp
    test_ofx_image_utils.cpp
    test_ofx_lifecycle.cpp
    test_ofx_model_selection.cpp
    test_ofx_output_modes.cpp
    test_ofx_runtime_client.cpp
    test_ofx_runtime_panel.cpp
    test_ofx_runtime_protocol.cpp
    test_output_path_utils.cpp
    test_process_paths.cpp
    test_runtime_diagnostics.cpp
    test_runtime_contracts.cpp
    test_session_cache_policy.cpp
    test_session_policy.cpp
    test_source_passthrough.cpp
    test_stage_profiler.cpp
    test_ofx_stubs.cpp
    test_tile_blend.cpp
    test_warmup_policy.cpp
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

corridorkey_stage_ort_runtime(test_unit)

# Target-based compiler warnings for tests
if(NOT MSVC)
    target_compile_options(test_unit PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    target_compile_options(test_unit PRIVATE /W4 /WX /permissive-)
endif()

add_test(NAME unit_tests COMMAND test_unit)
set_tests_properties(unit_tests PROPERTIES LABELS "unit")
