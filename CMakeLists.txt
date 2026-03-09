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

if(MSVC)
    add_compile_options(/W4 /WX /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
    
    # Address Sanitizer in Debug
    if(ENABLE_ASAN)
        add_compile_options(-fsanitize=address)
        add_link_options(-fsanitize=address)
    endif()
endif()

# --- Dependencies (vcpkg) ---

# vcpkg is handled automatically by the toolchain file in CMakePresets.json
# Find required packages
find_package(onnxruntime REQUIRED)
find_package(OpenEXR REQUIRED)
find_package(PNG REQUIRED)
find_package(FFMPEG REQUIRED)
find_package(Catch2 3 REQUIRED)

# --- Project Structure ---

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
