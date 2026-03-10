cmake_minimum_required(VERSION 3.28)

project(CorridorKey-Runtime
    VERSION 0.1.0
    DESCRIPTION "High-performance C++ inference runtime for CorridorKey"
    LANGUAGES CXX
)

# --- Project Rules & Standards ---

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Visibility control (hide everything by default)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Export compile commands for LSP (clangd)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Compiler Configuration ---
# Warning flags are applied per-target in src/CMakeLists.txt and tests/
# (No global add_compile_options — target-based approach only)

# --- Dependencies (vcpkg) ---

# vcpkg is handled automatically by the toolchain file in CMakePresets.json
# Find required packages

if(APPLE)
    # On macOS, use the vendored ONNX Runtime with CoreML support
    set(ONNXRUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/vendor/onnxruntime")
    
    # Create the imported target manually since we are skipping find_package config
    add_library(onnxruntime::onnxruntime SHARED IMPORTED)
    set_target_properties(onnxruntime::onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.dylib"
        INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_ROOT}/include"
    )
    
    # Ensure the executable can find the dylib at runtime
    set(CMAKE_BUILD_RPATH "${ONNXRUNTIME_ROOT}/lib")
    
    message(STATUS "Using vendored ONNX Runtime with CoreML support at: ${ONNXRUNTIME_ROOT}")
else()
    find_package(onnxruntime REQUIRED)
endif()

find_package(OpenEXR REQUIRED)
find_package(PNG REQUIRED)
find_package(FFMPEG REQUIRED)
find_package(cxxopts REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Catch2 3 REQUIRED)

# --- Project Structure ---

include(CTest)

# Public API headers
add_library(corridorkey_headers INTERFACE)
target_include_directories(corridorkey_headers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}> # Allow #include "vendor/..."
    $<INSTALL_INTERFACE:include>
)

# Core Library (shared by default for visibility testing)
add_subdirectory(src)

# CLI Application
add_subdirectory(src/cli)

# Testing
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
