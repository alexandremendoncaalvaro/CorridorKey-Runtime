# Core logic implementation files (Engine, Session, etc.)

target_sources(corridorkey_core PRIVATE
    engine.cpp
    device_detection.cpp
    gpu_prep.cpp
    gpu_resize.cpp
    inference_session.cpp
    ort_process_context.cpp
    mlx_memory_governor.cpp
    mlx_session.cpp
    mlx_probe.cpp
    windows_rtx_probe.cpp
    linux_cuda_probe.cpp
)

if(APPLE)
    # Objective-C++ probe that reads MTLDevice.recommendedMaxWorkingSetSize.
    # Kept on a dedicated .mm so the rest of the core stays pure C++20 and the
    # Metal framework link is scoped to the probe.
    target_sources(corridorkey_core PRIVATE metal_memory_probe.mm)
    target_link_libraries(corridorkey_core PRIVATE "-framework Metal" "-framework Foundation")
endif()

if(UNIX AND NOT APPLE)
    # NVML is resolved at runtime through dlopen(libnvidia-ml.so.1). We only
    # need -ldl here so dlopen/dlsym are linkable; NVML itself stays a soft
    # dependency, keeping the Linux build runnable without a GPU driver.
    target_link_libraries(corridorkey_core PRIVATE ${CMAKE_DL_LIBS})
endif()

# Strategy C, Sprint 1: optional TorchTRT in-process backend.
# Compiled into a SEPARATE static library so the libtorch warning
# relaxations (/wd4267 etc.) stay isolated to that TU - the rest of
# corridorkey_core stays under strict /W4 /WX. corridorkey_core links it
# privately and exposes a CORRIDORKEY_HAS_TORCHTRT compile define so the
# inference_session.cpp dispatch is enabled. Public header
# torch_trt_session.hpp is libtorch-free so the forward declaration in
# inference_session.hpp does not require torch on every consumer.
if(CORRIDORKEY_HAS_TORCHTRT)
    add_library(corridorkey_torchtrt_internal STATIC torch_trt_session.cpp)
    target_compile_features(corridorkey_torchtrt_internal PRIVATE cxx_std_20)
    target_include_directories(corridorkey_torchtrt_internal PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
    target_include_directories(corridorkey_torchtrt_internal SYSTEM PRIVATE
        "${CORRIDORKEY_TORCHTRT_INCLUDE_DIRS}"
        "${CORRIDORKEY_TORCHTRT_INCLUDE_DIRS}/torch/csrc/api/include"
    )
    target_link_libraries(corridorkey_torchtrt_internal
        PUBLIC
            corridorkey_headers
            nlohmann_json::nlohmann_json
        PRIVATE
            torch::torch
            torch::torch_cpu
            torch::torch_cuda
            torch::c10
            torch::c10_cuda
            torchtrt::torchtrt
    )
    if(MSVC)
        target_compile_options(corridorkey_torchtrt_internal PRIVATE
            /bigobj
            /wd4624 /wd4267 /wd4244 /wd4251 /wd4275 /wd4996
            /EHsc
        )
        target_compile_definitions(corridorkey_torchtrt_internal PRIVATE
            NOMINMAX WIN32_LEAN_AND_MEAN
        )
    endif()

    target_link_libraries(corridorkey_core PRIVATE
        corridorkey_torchtrt_internal
        torch::torch
        torch::torch_cpu
        torch::torch_cuda
        torch::c10
        torch::c10_cuda
        torchtrt::torchtrt
    )
    target_compile_definitions(corridorkey_core PRIVATE CORRIDORKEY_HAS_TORCHTRT=1)
endif()
