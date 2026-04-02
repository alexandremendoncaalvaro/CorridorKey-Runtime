cmake_minimum_required(VERSION 3.28)

if(APPLE)
    set(CORRIDORKEY_MACOS_DEPLOYMENT_TARGET "14.0" CACHE STRING
        "Minimum macOS deployment target for portable Apple Silicon releases")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "${CORRIDORKEY_MACOS_DEPLOYMENT_TARGET}" CACHE STRING
        "Minimum macOS deployment target for portable Apple Silicon releases" FORCE)
endif()

if(WIN32)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "" FORCE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)
endif()

project(CorridorKey-Runtime
    VERSION 0.9.3
    DESCRIPTION "High-performance C++ inference runtime for CorridorKey"
    LANGUAGES CXX
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/corridorkey/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/include/corridorkey/version.hpp
)

if(WIN32 AND MSVC)
    get_filename_component(_corridorkey_msvc_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_corridorkey_msvc_host_dir "${_corridorkey_msvc_bin_dir}" DIRECTORY)
    get_filename_component(_corridorkey_msvc_bin_root "${_corridorkey_msvc_host_dir}" DIRECTORY)
    get_filename_component(_corridorkey_msvc_root "${_corridorkey_msvc_bin_root}" DIRECTORY)
    set(CORRIDORKEY_MSVC_LIB_DIR "${_corridorkey_msvc_root}/lib/x64" CACHE INTERNAL
        "Resolved MSVC x64 library directory")
    set(CORRIDORKEY_MSVC_DELAYIMP_LIB "${CORRIDORKEY_MSVC_LIB_DIR}/delayimp.lib" CACHE INTERNAL
        "Resolved MSVC x64 delay import library")

    get_filename_component(_corridorkey_rc_arch_dir "${CMAKE_RC_COMPILER}" DIRECTORY)
    get_filename_component(_corridorkey_windows_sdk_version_dir "${_corridorkey_rc_arch_dir}" DIRECTORY)
    get_filename_component(_corridorkey_windows_sdk_bin_root "${_corridorkey_windows_sdk_version_dir}" DIRECTORY)
    get_filename_component(_corridorkey_windows_sdk_root "${_corridorkey_windows_sdk_bin_root}" DIRECTORY)
    get_filename_component(_corridorkey_windows_sdk_version
        "${_corridorkey_windows_sdk_version_dir}" NAME)
    set(CORRIDORKEY_WINDOWS_UCRT_LIB_DIR
        "${_corridorkey_windows_sdk_root}/Lib/${_corridorkey_windows_sdk_version}/ucrt/x64"
        CACHE INTERNAL "Resolved Windows SDK UCRT x64 library directory")
    set(CORRIDORKEY_WINDOWS_UM_LIB_DIR
        "${_corridorkey_windows_sdk_root}/Lib/${_corridorkey_windows_sdk_version}/um/x64"
        CACHE INTERNAL "Resolved Windows SDK UM x64 library directory")

    function(corridorkey_apply_windows_x64_link_dirs target_name)
        if(NOT TARGET ${target_name})
            message(FATAL_ERROR "corridorkey_apply_windows_x64_link_dirs expects an existing target")
        endif()
        target_link_directories(${target_name} PUBLIC
            "${CORRIDORKEY_MSVC_LIB_DIR}"
            "${CORRIDORKEY_WINDOWS_UCRT_LIB_DIR}"
            "${CORRIDORKEY_WINDOWS_UM_LIB_DIR}"
        )
    endfunction()
endif()

function(corridorkey_apply_windows_common_compile_defs target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "corridorkey_apply_windows_common_compile_defs expects an existing target")
    endif()
    if(WIN32)
        target_compile_definitions(${target_name} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    endif()
endfunction()

if(DEFINED CMAKE_TOOLCHAIN_FILE)
    message(STATUS "Using toolchain file: ${CMAKE_TOOLCHAIN_FILE}")
endif()

option(CORRIDORKEY_ENABLE_MLX "Enable optional MLX probe support on Apple platforms" ON)
set(CORRIDORKEY_MLX_CMAKE_DIR "" CACHE PATH "Optional MLX CMake package directory")
set(CORRIDORKEY_MLX_PYTHON "" CACHE FILEPATH "Optional Python interpreter used to discover MLX")
set(CORRIDORKEY_WINDOWS_ORT_ROOT "" CACHE PATH
    "Curated Windows ONNX Runtime root (vendor/onnxruntime-windows-rtx or vendor/onnxruntime-windows-dml)")
set(CORRIDORKEY_ORT_RUNTIME_DIR "" CACHE INTERNAL "Resolved ONNX Runtime DLL directory")
option(CORRIDORKEY_ENABLE_TORCH_TENSORRT
    "Enable optional Torch-TensorRT runtime support on Windows RTX" ON)
set(CORRIDORKEY_TORCH_CMAKE_DIR "" CACHE PATH "Optional Torch CMake package directory")
set(CORRIDORKEY_TORCH_RUNTIME_DIR "" CACHE INTERNAL "Resolved Torch runtime DLL directory")
set(CORRIDORKEY_TORCHTRT_RUNTIME_DIR "" CACHE INTERNAL
    "Resolved Torch-TensorRT runtime DLL directory")
set(CORRIDORKEY_TENSORRT_STANDARD_RUNTIME_DIR "" CACHE INTERNAL
    "Resolved standard TensorRT runtime DLL directory for Torch-TensorRT")
set(CORRIDORKEY_TORCHTRT_IMPORT_LIB "" CACHE INTERNAL
    "Resolved Torch-TensorRT import library")
set(CORRIDORKEY_TORCH_INCLUDE_DIRS "" CACHE INTERNAL "Resolved Torch C++ include directories")
set(CORRIDORKEY_TORCH_LIBRARIES "" CACHE INTERNAL "Resolved Torch C++ import libraries")

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
    set(ONNXRUNTIME_LIB_DIR "${ONNXRUNTIME_ROOT}/lib")

    if(EXISTS "${ONNXRUNTIME_LIB_DIR}/libonnxruntime.dylib")
        set(ONNXRUNTIME_DYLIB "${ONNXRUNTIME_LIB_DIR}/libonnxruntime.dylib")
    else()
        file(GLOB ONNXRUNTIME_DYLIB_CANDIDATES "${ONNXRUNTIME_LIB_DIR}/libonnxruntime*.dylib")
        list(FILTER ONNXRUNTIME_DYLIB_CANDIDATES EXCLUDE REGEX "\\.dSYM")
        if(NOT ONNXRUNTIME_DYLIB_CANDIDATES)
            message(FATAL_ERROR "Unable to locate vendored ONNX Runtime dylib in ${ONNXRUNTIME_LIB_DIR}")
        endif()
        list(GET ONNXRUNTIME_DYLIB_CANDIDATES 0 ONNXRUNTIME_DYLIB)
    endif()

    # Create the imported target manually since we are skipping find_package config
    add_library(onnxruntime::onnxruntime SHARED IMPORTED)
    set_target_properties(onnxruntime::onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ONNXRUNTIME_DYLIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_ROOT}/include"
    )

    # Ensure the executable can find the dylib at runtime
    set(CMAKE_BUILD_RPATH "${ONNXRUNTIME_LIB_DIR}")

    message(STATUS "Using vendored ONNX Runtime with CoreML support at: ${ONNXRUNTIME_DYLIB}")
    set(CORRIDORKEY_ORT_INCLUDE_DIRS "${ONNXRUNTIME_ROOT}/include")

    if(CORRIDORKEY_ENABLE_MLX)
        set(_corridorkey_mlx_package_dir "${CORRIDORKEY_MLX_CMAKE_DIR}")
        if(_corridorkey_mlx_package_dir STREQUAL "")
            foreach(_corridorkey_mlx_bottle_name IN ITEMS sonoma sequoia tahoe)
                file(GLOB _corridorkey_mlx_bottle_candidates
                     "${CMAKE_SOURCE_DIR}/build/mlx-${_corridorkey_mlx_bottle_name}-bottle/mlx/*/share/cmake/MLX")
                if(_corridorkey_mlx_bottle_candidates)
                    list(GET _corridorkey_mlx_bottle_candidates 0 _corridorkey_mlx_package_dir)
                    message(STATUS "Resolved portable MLX package at ${_corridorkey_mlx_package_dir}")
                    break()
                endif()
            endforeach()
        endif()

        if(_corridorkey_mlx_package_dir STREQUAL "")
            set(_corridorkey_mlx_python_candidates "")
            if(NOT CORRIDORKEY_MLX_PYTHON STREQUAL "")
                list(APPEND _corridorkey_mlx_python_candidates "${CORRIDORKEY_MLX_PYTHON}")
            endif()
            if(DEFINED ENV{VIRTUAL_ENV})
                list(APPEND _corridorkey_mlx_python_candidates "$ENV{VIRTUAL_ENV}/bin/python")
            endif()
            if(EXISTS "${CMAKE_SOURCE_DIR}/.venv-macos-mlx/bin/python")
                list(APPEND _corridorkey_mlx_python_candidates
                     "${CMAKE_SOURCE_DIR}/.venv-macos-mlx/bin/python")
            endif()

            find_package(Python3 COMPONENTS Interpreter QUIET)
            if(Python3_Interpreter_FOUND)
                list(APPEND _corridorkey_mlx_python_candidates "${Python3_EXECUTABLE}")
            endif()

            list(REMOVE_DUPLICATES _corridorkey_mlx_python_candidates)

            foreach(_corridorkey_mlx_python IN LISTS _corridorkey_mlx_python_candidates)
                if(NOT EXISTS "${_corridorkey_mlx_python}")
                    continue()
                endif()

                execute_process(
                    COMMAND "${_corridorkey_mlx_python}" -m mlx --cmake-dir
                    RESULT_VARIABLE CORRIDORKEY_MLX_CMAKE_DIR_RESULT
                    OUTPUT_VARIABLE _corridorkey_mlx_package_dir_candidate
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET
                )
                if(CORRIDORKEY_MLX_CMAKE_DIR_RESULT EQUAL 0 AND
                   NOT _corridorkey_mlx_package_dir_candidate STREQUAL "")
                    set(_corridorkey_mlx_package_dir "${_corridorkey_mlx_package_dir_candidate}")
                    message(STATUS "Resolved MLX CMake package with ${_corridorkey_mlx_python}")
                    break()
                endif()
            endforeach()
        endif()

        if(NOT _corridorkey_mlx_package_dir STREQUAL "")
            list(APPEND CMAKE_PREFIX_PATH "${_corridorkey_mlx_package_dir}")
        endif()

        find_package(MLX CONFIG QUIET)
        if(MLX_FOUND)
            if(_corridorkey_mlx_package_dir MATCHES "${CMAKE_SOURCE_DIR}/build/mlx-.*-bottle/.*/share/cmake/MLX$")
                get_filename_component(_corridorkey_mlx_prefix "${_corridorkey_mlx_package_dir}" DIRECTORY)
                get_filename_component(_corridorkey_mlx_prefix "${_corridorkey_mlx_prefix}" DIRECTORY)
                get_filename_component(_corridorkey_mlx_prefix "${_corridorkey_mlx_prefix}" DIRECTORY)
                set(_corridorkey_mlx_dylib "${_corridorkey_mlx_prefix}/lib/libmlx.dylib")
                if(EXISTS "${_corridorkey_mlx_dylib}")
                    execute_process(
                        COMMAND install_name_tool -id @rpath/libmlx.dylib "${_corridorkey_mlx_dylib}"
                        RESULT_VARIABLE _corridorkey_mlx_install_name_result
                        ERROR_VARIABLE _corridorkey_mlx_install_name_error
                    )
                    if(NOT _corridorkey_mlx_install_name_result EQUAL 0)
                        message(FATAL_ERROR
                            "Failed to normalize portable MLX install_name: ${_corridorkey_mlx_install_name_error}")
                    endif()
                    execute_process(
                        COMMAND codesign --force --sign - "${_corridorkey_mlx_dylib}"
                        RESULT_VARIABLE _corridorkey_mlx_codesign_result
                        ERROR_VARIABLE _corridorkey_mlx_codesign_error
                    )
                    if(NOT _corridorkey_mlx_codesign_result EQUAL 0)
                        message(FATAL_ERROR
                            "Failed to ad-hoc sign portable MLX dylib: ${_corridorkey_mlx_codesign_error}")
                    endif()
                endif()
            endif()
            if(TARGET mlx)
                # The packaged MLX target hardcodes framework paths under
                # /Applications/Xcode-latest.app, which is not present on
                # machines using Command Line Tools only. Replace those
                # interface links with portable framework directives.
                set_property(TARGET mlx PROPERTY INTERFACE_LINK_LIBRARIES
                    "-framework Metal;-framework Foundation;-framework QuartzCore;-framework Accelerate"
                )
            endif()
            message(STATUS "MLX probe support enabled")
        else()
            message(STATUS "MLX probe support unavailable; continuing without MLX integration")
        endif()
    endif()
else()
    if(WIN32)
        set(_corridorkey_windows_ort_root "${CORRIDORKEY_WINDOWS_ORT_ROOT}")
        if(_corridorkey_windows_ort_root STREQUAL "" AND
           EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/onnxruntime-windows-rtx")
            set(_corridorkey_windows_ort_root
                "${CMAKE_CURRENT_SOURCE_DIR}/vendor/onnxruntime-windows-rtx")
        endif()
        if(_corridorkey_windows_ort_root STREQUAL "" AND
           EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/onnxruntime-windows-dml")
            set(_corridorkey_windows_ort_root
                "${CMAKE_CURRENT_SOURCE_DIR}/vendor/onnxruntime-windows-dml")
        endif()

        if(NOT _corridorkey_windows_ort_root STREQUAL "")
            set(_corridorkey_windows_ort_include "${_corridorkey_windows_ort_root}/include")
            set(_corridorkey_windows_ort_dll "${_corridorkey_windows_ort_root}/bin/onnxruntime.dll")
            if(NOT EXISTS "${_corridorkey_windows_ort_dll}")
                set(_corridorkey_windows_ort_dll "${_corridorkey_windows_ort_root}/lib/onnxruntime.dll")
            endif()
            set(_corridorkey_windows_ort_implib "${_corridorkey_windows_ort_root}/lib/onnxruntime.lib")
            if(EXISTS "${_corridorkey_windows_ort_include}" AND EXISTS "${_corridorkey_windows_ort_dll}" AND
               EXISTS "${_corridorkey_windows_ort_implib}")
                add_library(onnxruntime::onnxruntime SHARED IMPORTED)
                set_target_properties(onnxruntime::onnxruntime PROPERTIES
                    IMPORTED_LOCATION "${_corridorkey_windows_ort_dll}"
                    IMPORTED_IMPLIB "${_corridorkey_windows_ort_implib}"
                    # NOTE: We don't set INTERFACE_INCLUDE_DIRECTORIES here to avoid
                    # conflicts with vcpkg's ONNX Runtime. Include paths are set directly
                    # in target_include_directories with BEFORE to take precedence.
                )
                set(CORRIDORKEY_ORT_INCLUDE_DIRS "${_corridorkey_windows_ort_include}")
                get_filename_component(CORRIDORKEY_ORT_RUNTIME_DIR
                    "${_corridorkey_windows_ort_dll}" DIRECTORY)
                message(STATUS "Using curated Windows ONNX Runtime at: ${_corridorkey_windows_ort_root}")
            else()
                message(FATAL_ERROR
                    "Configured CORRIDORKEY_WINDOWS_ORT_ROOT is missing include/onnxruntime or lib/onnxruntime.{dll,lib}: ${_corridorkey_windows_ort_root}")
            endif()
        else()
            message(FATAL_ERROR
                "Windows builds require a curated ONNX Runtime root. Set CORRIDORKEY_WINDOWS_ORT_ROOT or stage vendor/onnxruntime-windows-rtx or vendor/onnxruntime-windows-dml.")
        endif()
    else()
        find_package(onnxruntime REQUIRED)
        set(CORRIDORKEY_ORT_INCLUDE_DIRS ${onnxruntime_INCLUDE_DIRS})
    endif()
endif()

find_package(OpenEXR REQUIRED)
find_package(PNG REQUIRED)
find_package(FFMPEG REQUIRED)
find_package(cxxopts REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Catch2 3 REQUIRED)

set(CORRIDORKEY_WITH_TORCH_TENSORRT OFF)
if(WIN32 AND CORRIDORKEY_ENABLE_TORCH_TENSORRT)
    set(_corridorkey_torch_site_packages
        "${CMAKE_SOURCE_DIR}/tools/model_exporter/.venv_torchtrt/Lib/site-packages")
    set(_corridorkey_torch_include_dir "${_corridorkey_torch_site_packages}/torch/include")
    set(_corridorkey_torch_api_include_dir
        "${_corridorkey_torch_include_dir}/torch/csrc/api/include")
    set(_corridorkey_torch_runtime_dir "${_corridorkey_torch_site_packages}/torch/lib")
    set(_corridorkey_torchtrt_runtime_dir "${_corridorkey_torch_site_packages}/torch_tensorrt/lib")
    set(_corridorkey_standard_tensorrt_runtime_dir
        "${_corridorkey_torch_site_packages}/tensorrt_libs")

    file(GLOB _corridorkey_cuda_include_candidates
        "${_corridorkey_torch_site_packages}/nvidia/*/include/cuda_runtime_api.h")
    set(_corridorkey_cuda_include_dir "")
    if(_corridorkey_cuda_include_candidates)
        list(GET _corridorkey_cuda_include_candidates 0 _corridorkey_cuda_header)
        get_filename_component(_corridorkey_cuda_include_dir "${_corridorkey_cuda_header}" DIRECTORY)
    endif()

    set(_corridorkey_torch_import_libs "")
    foreach(_corridorkey_torch_lib_name IN ITEMS torch c10 torch_cpu torch_cuda c10_cuda caffe2_nvrtc)
        set(_corridorkey_torch_lib_path
            "${_corridorkey_torch_runtime_dir}/${_corridorkey_torch_lib_name}.lib")
        if(EXISTS "${_corridorkey_torch_lib_path}")
            list(APPEND _corridorkey_torch_import_libs "${_corridorkey_torch_lib_path}")
        endif()
    endforeach()
    set(_corridorkey_torchtrt_import_lib "${_corridorkey_torchtrt_runtime_dir}/torchtrt.lib")
    if(EXISTS "${_corridorkey_torchtrt_import_lib}")
        list(APPEND _corridorkey_torch_import_libs "${_corridorkey_torchtrt_import_lib}")
    endif()

    set(_corridorkey_torch_include_dirs "")
    foreach(_corridorkey_include_dir IN ITEMS
            "${_corridorkey_torch_include_dir}"
            "${_corridorkey_torch_api_include_dir}"
            "${_corridorkey_cuda_include_dir}")
        if(NOT _corridorkey_include_dir STREQUAL "" AND EXISTS "${_corridorkey_include_dir}")
            list(APPEND _corridorkey_torch_include_dirs "${_corridorkey_include_dir}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _corridorkey_torch_include_dirs)

    if(EXISTS "${_corridorkey_torch_include_dir}" AND
       EXISTS "${_corridorkey_torch_api_include_dir}" AND
       NOT _corridorkey_cuda_include_dir STREQUAL "" AND
       EXISTS "${_corridorkey_torch_runtime_dir}" AND
       NOT _corridorkey_torchtrt_runtime_dir STREQUAL "" AND
       EXISTS "${_corridorkey_torchtrt_runtime_dir}" AND
       NOT _corridorkey_standard_tensorrt_runtime_dir STREQUAL "" AND
       EXISTS "${_corridorkey_standard_tensorrt_runtime_dir}" AND
       _corridorkey_torch_import_libs)
        set(CORRIDORKEY_WITH_TORCH_TENSORRT ON)
        set(CORRIDORKEY_TORCH_INCLUDE_DIRS "${_corridorkey_torch_include_dirs}" CACHE INTERNAL
            "Resolved Torch C++ include directories" FORCE)
        set(CORRIDORKEY_TORCH_LIBRARIES "${_corridorkey_torch_import_libs}" CACHE INTERNAL
            "Resolved Torch C++ import libraries" FORCE)
        set(CORRIDORKEY_TORCH_RUNTIME_DIR "${_corridorkey_torch_runtime_dir}" CACHE INTERNAL
            "Resolved Torch runtime DLL directory" FORCE)
        set(CORRIDORKEY_TORCHTRT_RUNTIME_DIR "${_corridorkey_torchtrt_runtime_dir}" CACHE INTERNAL
            "Resolved Torch-TensorRT runtime DLL directory" FORCE)
        set(CORRIDORKEY_TENSORRT_STANDARD_RUNTIME_DIR
            "${_corridorkey_standard_tensorrt_runtime_dir}" CACHE INTERNAL
            "Resolved standard TensorRT runtime DLL directory for Torch-TensorRT" FORCE)
        set(CORRIDORKEY_TORCHTRT_IMPORT_LIB "${_corridorkey_torchtrt_import_lib}" CACHE INTERNAL
            "Resolved Torch-TensorRT import library" FORCE)
        message(STATUS "Torch-TensorRT runtime support enabled from the local Python wheel layout")
    else()
        message(STATUS "Torch C++ runtime not found; building without Torch-TensorRT support")
    endif()
endif()

function(corridorkey_stage_windows_runtime target_name)
    if(NOT WIN32)
        return()
    endif()

    set(_corridorkey_runtime_dlls "")
    if(CORRIDORKEY_ORT_RUNTIME_DIR)
        file(GLOB _corridorkey_ort_runtime_dlls "${CORRIDORKEY_ORT_RUNTIME_DIR}/*.dll")
        list(APPEND _corridorkey_runtime_dlls ${_corridorkey_ort_runtime_dlls})
    endif()
    if(CORRIDORKEY_TORCH_RUNTIME_DIR)
        file(GLOB _corridorkey_torch_runtime_dlls "${CORRIDORKEY_TORCH_RUNTIME_DIR}/*.dll")
        list(APPEND _corridorkey_runtime_dlls ${_corridorkey_torch_runtime_dlls})
    endif()
    if(CORRIDORKEY_TORCHTRT_RUNTIME_DIR)
        file(GLOB _corridorkey_torchtrt_runtime_dlls "${CORRIDORKEY_TORCHTRT_RUNTIME_DIR}/*.dll")
        list(APPEND _corridorkey_runtime_dlls ${_corridorkey_torchtrt_runtime_dlls})
    endif()
    if(CORRIDORKEY_TENSORRT_STANDARD_RUNTIME_DIR)
        file(GLOB _corridorkey_standard_tensorrt_dlls
            "${CORRIDORKEY_TENSORRT_STANDARD_RUNTIME_DIR}/*.dll")
        list(APPEND _corridorkey_runtime_dlls ${_corridorkey_standard_tensorrt_dlls})
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${target_name}>
            ${_corridorkey_runtime_dlls}
            $<TARGET_FILE_DIR:${target_name}>
        COMMAND_EXPAND_LISTS
        COMMENT "Staging Windows runtime DLLs for ${target_name}"
    )
endfunction()

# --- Project Structure ---

include(CTest)

# Public API headers
add_library(corridorkey_headers INTERFACE)
target_include_directories(corridorkey_headers INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/include>
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
