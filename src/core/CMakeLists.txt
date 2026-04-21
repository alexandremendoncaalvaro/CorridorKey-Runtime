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

if(UNIX AND NOT APPLE)
    # NVML is resolved at runtime through dlopen(libnvidia-ml.so.1). We only
    # need -ldl here so dlopen/dlsym are linkable; NVML itself stays a soft
    # dependency, keeping the Linux build runnable without a GPU driver.
    target_link_libraries(corridorkey_core PRIVATE ${CMAKE_DL_LIBS})
endif()
