# GPU tests (real CUDA / NPP hardware)
#
# These tests run NPP kernels against a live CUDA device. They live in their
# own executable because linking them into test_unit or test_e2e exposes a
# CUDA context ordering issue: once the process has constructed any ONNX
# Runtime session (even CPU-backed), the first NPP call that follows can
# crash under randomized Catch2 ordering. Isolating these tests in their
# own binary guarantees a clean CUDA context for every ctest invocation.

add_executable(test_gpu
    test_gpu_prep.cpp
    test_gpu_resize.cpp
)

corridorkey_apply_windows_common_compile_defs(test_gpu)

target_link_libraries(test_gpu
    PRIVATE
        corridorkey_core_static
        Catch2::Catch2WithMain
)

corridorkey_stage_ort_runtime(test_gpu)

if(NOT MSVC)
    target_compile_options(test_gpu PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    target_compile_options(test_gpu PRIVATE /W4 /WX /permissive-)
endif()

add_test(NAME gpu_tests COMMAND test_gpu)
set_tests_properties(gpu_tests PROPERTIES LABELS "gpu")
