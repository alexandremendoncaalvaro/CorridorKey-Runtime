# I/O implementation files (EXR, PNG, FFmpeg)

target_sources(corridorkey_core PRIVATE
    frame_io.cpp
    exr_io.cpp
    png_io.cpp
    video_io.cpp
)
