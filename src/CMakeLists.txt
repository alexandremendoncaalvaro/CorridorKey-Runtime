# Core implementation targets

# Collect source files for the core library
# This is where we handle the PIMPL implementation and internal details

add_library(corridorkey_core SHARED)

# Private includes for internal implementation headers
target_include_directories(corridorkey_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link against public headers and external libraries
target_link_libraries(corridorkey_core
    PUBLIC
        corridorkey_headers
    PRIVATE
        onnxruntime::onnxruntime
        OpenEXR::OpenEXR
        libpng::libpng
        FFmpeg::avcodec
        FFmpeg::avformat
        FFmpeg::swscale
        FFmpeg::swresample
)

# Set the compile definition for the export macro
target_compile_definitions(corridorkey_core PRIVATE CORRIDORKEY_EXPORTS)

# Organize subdirectories
add_subdirectory(core)
add_subdirectory(frame_io)
add_subdirectory(post_process)

# Add source files to the target as they are created in subdirectories
# (We will use target_sources in sub-CMakeLists.txt)
