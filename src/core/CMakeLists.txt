# Core logic implementation files (Engine, Session, etc.)

target_sources(corridorkey_core PRIVATE
    engine.cpp
    device_detection.cpp
    inference_session.cpp
    mlx_session.cpp
    mlx_probe.cpp
    torch_trt_session.cpp
    windows_rtx_probe.cpp
)

if(MSVC AND CORRIDORKEY_WITH_TORCH_TENSORRT)
    set_source_files_properties(torch_trt_session.cpp PROPERTIES
        COMPILE_OPTIONS "/wd4244;/wd4267"
    )
endif()
