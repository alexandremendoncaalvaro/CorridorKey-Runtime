# Core implementation targets

# Collect source files for the core library
# This is where we handle the PIMPL implementation and internal details

if(WIN32)
    # Force static for all internal targets on Windows
    add_library(corridorkey_core STATIC)
    target_compile_definitions(corridorkey_core PUBLIC CORRIDORKEY_STATIC_DEFINE)
else()
    add_library(corridorkey_core SHARED)
endif()

corridorkey_apply_windows_common_compile_defs(corridorkey_core)

# Private includes for internal implementation headers
if(APPLE)
    target_include_directories(corridorkey_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${FFMPEG_INCLUDE_DIRS}
        "${CMAKE_SOURCE_DIR}/vendor/onnxruntime/include"
    )
else()
    target_include_directories(corridorkey_core PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${FFMPEG_INCLUDE_DIRS}
    )
    if(CORRIDORKEY_ORT_INCLUDE_DIRS)
        target_include_directories(corridorkey_core BEFORE PRIVATE
            ${CORRIDORKEY_ORT_INCLUDE_DIRS}
            "${CORRIDORKEY_ORT_INCLUDE_DIRS}/onnxruntime/core/session"
        )
    endif()
    # CRITICAL: Override vcpkg's ONNX Runtime with vendor version
    # For GCC: use -I flag which takes precedence over -isystem
    if(NOT MSVC AND CORRIDORKEY_ORT_INCLUDE_DIRS)
        target_compile_options(corridorkey_core PRIVATE
            "-I${CORRIDORKEY_ORT_INCLUDE_DIRS}"
        )
    endif()
endif()

# Strict compiler warnings (target-based, not global)
if(MSVC)
    target_compile_options(corridorkey_core PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(corridorkey_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(ENABLE_ASAN)
        target_compile_options(corridorkey_core PRIVATE -fsanitize=address)
        target_link_options(corridorkey_core PRIVATE -fsanitize=address)
    endif()
endif()

# Link against public headers and external libraries
target_link_libraries(corridorkey_core
    PUBLIC
        corridorkey_headers
        nlohmann_json::nlohmann_json
    PRIVATE
        onnxruntime::onnxruntime
        OpenEXR::OpenEXR
        PNG::PNG
        ${FFMPEG_LIBRARIES}
)

if(APPLE)
    target_link_libraries(corridorkey_core PRIVATE "-framework CoreML" "-framework Foundation" "-framework Accelerate")
    target_link_options(corridorkey_core PRIVATE "-Wl,-no_warn_duplicate_libraries")
endif()

if(WIN32)
    target_link_libraries(corridorkey_core PRIVATE dxgi ws2_32 ncrypt)
    corridorkey_apply_windows_x64_link_dirs(corridorkey_core)
    if(CORRIDORKEY_HAS_CUDA)
        target_link_libraries(corridorkey_core PRIVATE CUDA::cudart_static CUDA::nppc CUDA::nppig CUDA::nppidei CUDA::nppial)
        target_compile_definitions(corridorkey_core PRIVATE CORRIDORKEY_HAS_CUDA=1)
    endif()
endif()

if(APPLE AND MLX_FOUND)
    target_link_libraries(corridorkey_core PRIVATE mlx)
    target_compile_definitions(corridorkey_core PRIVATE CORRIDORKEY_WITH_MLX=1)
else()
    target_compile_definitions(corridorkey_core PRIVATE CORRIDORKEY_WITH_MLX=0)
endif()

# Set the compile definition for the export macro
target_compile_definitions(corridorkey_core PRIVATE CORRIDORKEY_EXPORTS)

target_sources(corridorkey_core PRIVATE
    common/local_ipc.cpp
    common/shared_memory_transport.cpp
)

# Relax warnings for png_io.cpp because of stb library legacy code
if(MSVC)
    set_source_files_properties(frame_io/png_io.cpp PROPERTIES
        COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS"
    )
else()
    set_source_files_properties(frame_io/png_io.cpp PROPERTIES
        COMPILE_FLAGS "-Wno-error -Wno-deprecated-declarations -Wno-missing-field-initializers"
    )
endif()

# Organize subdirectories
add_subdirectory(core)
add_subdirectory(app)
add_subdirectory(frame_io)
add_subdirectory(post_process)
add_subdirectory(plugins/ofx)

# Static library for testing (exposes all symbols without CORRIDORKEY_API)
if(BUILD_TESTING)
    add_library(corridorkey_core_static STATIC)
    corridorkey_apply_windows_common_compile_defs(corridorkey_core_static)

    # Share the same source files
    get_target_property(_core_sources corridorkey_core SOURCES)
    target_sources(corridorkey_core_static PRIVATE ${_core_sources})

    if(APPLE)
        target_include_directories(corridorkey_core_static PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${FFMPEG_INCLUDE_DIRS}
            "${CMAKE_SOURCE_DIR}/vendor/onnxruntime/include"
        )
    else()
        target_include_directories(corridorkey_core_static PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${FFMPEG_INCLUDE_DIRS}
            ${CORRIDORKEY_ORT_INCLUDE_DIRS}
            "${CORRIDORKEY_ORT_INCLUDE_DIRS}/onnxruntime/core/session"
        )
    endif()

    target_link_libraries(corridorkey_core_static
        PUBLIC
            corridorkey_headers
            onnxruntime::onnxruntime
            nlohmann_json::nlohmann_json
        PRIVATE
            OpenEXR::OpenEXR
            PNG::PNG
            ${FFMPEG_LIBRARIES}
    )

    if(APPLE)
        target_link_libraries(corridorkey_core_static PRIVATE "-framework CoreML" "-framework Foundation" "-framework Accelerate")
    endif()

    if(WIN32)
        target_link_libraries(corridorkey_core_static PRIVATE dxgi ws2_32 ncrypt)
        corridorkey_apply_windows_x64_link_dirs(corridorkey_core_static)
        if(CORRIDORKEY_HAS_CUDA)
            target_link_libraries(corridorkey_core_static PRIVATE CUDA::cudart_static CUDA::nppc CUDA::nppig CUDA::nppidei CUDA::nppial)
            target_compile_definitions(corridorkey_core_static PRIVATE CORRIDORKEY_HAS_CUDA=1)
        endif()
    endif()

    if(APPLE AND MLX_FOUND)
        target_link_libraries(corridorkey_core_static PRIVATE mlx)
        target_compile_definitions(corridorkey_core_static PRIVATE CORRIDORKEY_WITH_MLX=1)
    else()
        target_compile_definitions(corridorkey_core_static PRIVATE CORRIDORKEY_WITH_MLX=0)
    endif()

    target_compile_definitions(corridorkey_core_static PUBLIC CORRIDORKEY_STATIC_DEFINE)

    target_sources(corridorkey_core_static PRIVATE
        common/local_ipc.cpp
        common/shared_memory_transport.cpp
    )

    if(NOT MSVC)
        set_source_files_properties(frame_io/png_io.cpp PROPERTIES
            COMPILE_FLAGS "-Wno-error -Wno-deprecated-declarations -Wno-missing-field-initializers"
        )
        target_compile_options(corridorkey_core_static PRIVATE -Wall -Wextra -Wpedantic -Werror)
    else()
        target_compile_options(corridorkey_core_static PRIVATE /W4 /WX /permissive-)
    endif()
endif()
