# Core implementation targets

# Collect source files for the core library
# This is where we handle the PIMPL implementation and internal details

# corridorkey_common: ORT-free static library that hosts the parts of the
# runtime that do NOT need to talk to ONNX Runtime directly (IPC wire codec,
# pure types, frame I/O, post-process pixel math, version check, runtime
# contracts policy). The .ofx plugin links exclusively against this library
# so its import table never names onnxruntime.dll. Foundry Nuke ships its
# own CUDA stack next to its executable, and the Win32 loader resolves
# `LoadLibrary("cudart64_12.dll")` against the first module in the process
# with that base name regardless of search path or LOAD_WITH_ALTERED_SEARCH_PATH
# (https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexa).
# Keeping ORT entirely out of the plugin's link line is the only documented
# way to prevent the host's CUDA from binding to our delay-loaded sibling
# DLLs and tripping ERROR_DLL_INIT_FAILED (1114) inside the TensorRT-RTX
# provider's DllMain. All inference work runs out-of-process in
# corridorkey_ofx_runtime_server.exe, which has its own loader namespace.
add_library(corridorkey_common STATIC)
target_compile_definitions(corridorkey_common PUBLIC CORRIDORKEY_STATIC_DEFINE)
corridorkey_apply_windows_common_compile_defs(corridorkey_common)

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

# corridorkey_common build settings (mirrors corridorkey_core where
# applicable but never links against onnxruntime).
target_include_directories(corridorkey_common PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FFMPEG_INCLUDE_DIRS}
)

if(MSVC)
    target_compile_options(corridorkey_common PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(corridorkey_common PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(ENABLE_ASAN)
        target_compile_options(corridorkey_common PRIVATE -fsanitize=address)
        target_link_options(corridorkey_common PRIVATE -fsanitize=address)
    endif()
endif()

target_link_libraries(corridorkey_common
    PUBLIC
        corridorkey_headers
        nlohmann_json::nlohmann_json
    PRIVATE
        OpenEXR::OpenEXR
        PNG::PNG
        ${FFMPEG_LIBRARIES}
        cpr::cpr
)

if(WIN32)
    target_link_libraries(corridorkey_common PRIVATE ws2_32)
    corridorkey_apply_windows_x64_link_dirs(corridorkey_common)
endif()

target_compile_definitions(corridorkey_common PRIVATE CORRIDORKEY_EXPORTS)

# corridorkey_core depends PUBLIC on corridorkey_common so existing consumers
# (CLI, OFX runtime server) keep working with a single link line.
target_link_libraries(corridorkey_core PUBLIC corridorkey_common)

target_sources(corridorkey_common PRIVATE
    common/host_memory.cpp
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

    # Share the same source files. Pull from both corridorkey_core (the
    # ORT-using slice) and corridorkey_common (the ORT-free slice) so the
    # test static library exposes every internal symbol without rewriting
    # the per-subdir source lists.
    get_target_property(_core_sources corridorkey_core SOURCES)
    target_sources(corridorkey_core_static PRIVATE ${_core_sources})
    get_target_property(_common_sources corridorkey_common SOURCES)
    target_sources(corridorkey_core_static PRIVATE ${_common_sources})

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
            cpr::cpr
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

    if(NOT MSVC)
        set_source_files_properties(frame_io/png_io.cpp PROPERTIES
            COMPILE_FLAGS "-Wno-error -Wno-deprecated-declarations -Wno-missing-field-initializers"
        )
        target_compile_options(corridorkey_core_static PRIVATE -Wall -Wextra -Wpedantic -Werror)
    else()
        target_compile_options(corridorkey_core_static PRIVATE /W4 /WX /permissive-)
    endif()
endif()
