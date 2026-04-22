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
