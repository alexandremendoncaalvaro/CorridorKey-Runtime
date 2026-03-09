# Core implementation targets

# Collect source files for the core library
# This is where we handle the PIMPL implementation and internal details

add_library(corridorkey_core SHARED)

# Private includes for internal implementation headers
target_include_directories(corridorkey_core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FFMPEG_INCLUDE_DIRS}
    ${onnxruntime_INCLUDE_DIRS}
)

# Link against public headers and external libraries
target_link_libraries(corridorkey_core
    PUBLIC
        corridorkey_headers
    PRIVATE
        onnxruntime::onnxruntime
        OpenEXR::OpenEXR
        PNG::PNG
        ${FFMPEG_LIBRARIES}
)

if(APPLE)
    target_link_libraries(corridorkey_core PRIVATE "-framework CoreML" "-framework Foundation")
endif()

# Set the compile definition for the export macro
target_compile_definitions(corridorkey_core PRIVATE CORRIDORKEY_EXPORTS)

# Relax warnings for png_io.cpp because of stb library legacy code
if(NOT MSVC)
    set_source_files_properties(frame_io/png_io.cpp PROPERTIES COMPILE_FLAGS "-Wno-error")
endif()

# Organize subdirectories
add_subdirectory(core)
add_subdirectory(frame_io)
add_subdirectory(post_process)

# Add source files to the target as they are created in subdirectories
# (We will use target_sources in sub-CMakeLists.txt)
