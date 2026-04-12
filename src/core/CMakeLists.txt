# Core logic implementation files (Engine, Session, etc.)

target_sources(corridorkey_core PRIVATE
    engine.cpp
    device_detection.cpp
    gpu_prep.cpp
    gpu_resize.cpp
    inference_session.cpp
    ort_process_context.cpp
    mlx_session.cpp
    mlx_probe.cpp
    windows_rtx_probe.cpp
)
