# Core logic implementation files (Engine, Session, etc.)

target_sources(corridorkey_core PRIVATE
    engine.cpp
    device_detection.cpp
    inference_session.cpp
    mlx_probe.cpp
)
