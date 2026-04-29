# I/O implementation files (EXR, PNG, FFmpeg). All ORT-free, so they live
# in corridorkey_common to keep the .ofx plugin's import table clean.

target_sources(corridorkey_common PRIVATE
    frame_io.cpp
    exr_io.cpp
    png_io.cpp
    video_io.cpp
)
