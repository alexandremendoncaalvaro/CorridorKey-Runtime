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
    VERSION 0.8.3
    DESCRIPTION "High-performance C++ inference runtime for CorridorKey"
    LANGUAGES CXX
)

set(CORRIDORKEY_DISPLAY_VERSION_LABEL "" CACHE STRING
    "Optional user-facing build label for local optimization checkpoint builds")

if(CORRIDORKEY_DISPLAY_VERSION_LABEL)
    set(CORRIDORKEY_VERSION_DISPLAY_STRING "${CORRIDORKEY_DISPLAY_VERSION_LABEL}")
else()
    set(CORRIDORKEY_VERSION_DISPLAY_STRING "${PROJECT_VERSION}")
endif()

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
set(CORRIDORKEY_LINUX_ORT_ROOT "" CACHE PATH
    "Curated Linux ONNX Runtime root (vendor/onnxruntime-linux-cuda)")
set(CORRIDORKEY_ORT_RUNTIME_DIR "" CACHE INTERNAL "Resolved ONNX Runtime runtime library directory")

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
            # find_package() persists <PackageName>_DIR in the cache, so an
            # older build directory can keep resolving MLX from a developer
            # virtualenv even after a portable bottle becomes available.
            # Force the canonical package dir to keep all macOS builds aligned
            # on the same portable dependency set.
            set(MLX_DIR "${_corridorkey_mlx_package_dir}" CACHE PATH
                "Resolved MLX CMake package directory" FORCE)
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
    elseif(UNIX AND NOT APPLE)
        set(_corridorkey_linux_ort_root "${CORRIDORKEY_LINUX_ORT_ROOT}")
        if(_corridorkey_linux_ort_root STREQUAL "" AND
           EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/onnxruntime-linux-cuda")
            set(_corridorkey_linux_ort_root
                "${CMAKE_CURRENT_SOURCE_DIR}/vendor/onnxruntime-linux-cuda")
        endif()

        if(NOT _corridorkey_linux_ort_root STREQUAL "")
            set(_corridorkey_linux_ort_include "${_corridorkey_linux_ort_root}/include")
            set(_corridorkey_linux_ort_lib
                "${_corridorkey_linux_ort_root}/lib/libonnxruntime.so")
            if(EXISTS "${_corridorkey_linux_ort_include}" AND
               EXISTS "${_corridorkey_linux_ort_lib}")
                add_library(onnxruntime::onnxruntime SHARED IMPORTED)
                set_target_properties(onnxruntime::onnxruntime PROPERTIES
                    IMPORTED_LOCATION "${_corridorkey_linux_ort_lib}"
                )
                set(CORRIDORKEY_ORT_INCLUDE_DIRS "${_corridorkey_linux_ort_include}")
                set(CORRIDORKEY_ORT_RUNTIME_DIR "${_corridorkey_linux_ort_root}/lib")
                message(STATUS "Using curated Linux ONNX Runtime at: ${_corridorkey_linux_ort_root}")
            else()
                message(FATAL_ERROR
                    "Configured CORRIDORKEY_LINUX_ORT_ROOT is missing include or lib/libonnxruntime.so: ${_corridorkey_linux_ort_root}")
            endif()
        else()
            find_package(onnxruntime REQUIRED)
            set(CORRIDORKEY_ORT_INCLUDE_DIRS ${onnxruntime_INCLUDE_DIRS})
        endif()
    else()
        find_package(onnxruntime REQUIRED)
        set(CORRIDORKEY_ORT_INCLUDE_DIRS ${onnxruntime_INCLUDE_DIRS})
    endif()
endif()

if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/onnxruntime-windows-rtx")
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
        message(STATUS "CUDA Toolkit ${CUDAToolkit_VERSION} found — pinned-host and NPP paths available")
        set(CORRIDORKEY_HAS_CUDA TRUE)
    else()
        message(STATUS "CUDA Toolkit not found — pinned-host and NPP paths disabled")
        set(CORRIDORKEY_HAS_CUDA FALSE)
    endif()
endif()

# Optional Windows TorchTRT runtime payload (blue-pack backend, Sprint 1).
# Staged by `scripts/windows.ps1 -Task prepare-torchtrt`. Absence is fine —
# blocks below in src/core only enable the TorchTrtSession when the imported
# targets are defined, so green continues to build with ONNX→TRT-RTX EP only.
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/torchtrt-windows")
    set(_corridorkey_torchtrt_root "${CMAKE_CURRENT_SOURCE_DIR}/vendor/torchtrt-windows")
    set(_corridorkey_torchtrt_bin "${_corridorkey_torchtrt_root}/bin")
    set(_corridorkey_torchtrt_lib "${_corridorkey_torchtrt_root}/lib")
    set(_corridorkey_torchtrt_include "${_corridorkey_torchtrt_root}/include/torch")

    function(_corridorkey_declare_torchtrt_target target_name dll_name lib_name)
        set(_dll "${_corridorkey_torchtrt_bin}/${dll_name}")
        set(_lib "${_corridorkey_torchtrt_lib}/${lib_name}")
        if(EXISTS "${_dll}" AND EXISTS "${_lib}")
            add_library(${target_name} SHARED IMPORTED)
            set_target_properties(${target_name} PROPERTIES
                IMPORTED_LOCATION "${_dll}"
                IMPORTED_IMPLIB "${_lib}"
            )
        endif()
    endfunction()

    _corridorkey_declare_torchtrt_target(torch::c10 c10.dll c10.lib)
    _corridorkey_declare_torchtrt_target(torch::c10_cuda c10_cuda.dll c10_cuda.lib)
    _corridorkey_declare_torchtrt_target(torch::torch torch.dll torch.lib)
    _corridorkey_declare_torchtrt_target(torch::torch_cpu torch_cpu.dll torch_cpu.lib)
    _corridorkey_declare_torchtrt_target(torch::torch_cuda torch_cuda.dll torch_cuda.lib)
    _corridorkey_declare_torchtrt_target(torchtrt::torchtrt torchtrt.dll torchtrt.lib)

    if(TARGET torch::torch AND TARGET torchtrt::torchtrt)
        set(CORRIDORKEY_HAS_TORCHTRT TRUE)
        set(CORRIDORKEY_TORCHTRT_INCLUDE_DIRS "${_corridorkey_torchtrt_include}")
        set(CORRIDORKEY_TORCHTRT_RUNTIME_DIR "${_corridorkey_torchtrt_bin}")
        message(STATUS "Using vendored Windows TorchTRT runtime at: ${_corridorkey_torchtrt_root}")
    else()
        set(CORRIDORKEY_HAS_TORCHTRT FALSE)
        message(WARNING
            "vendor/torchtrt-windows is present but expected DLLs/import libs are missing. "
            "Re-run scripts/windows.ps1 -Task prepare-torchtrt.")
    endif()
else()
    set(CORRIDORKEY_HAS_TORCHTRT FALSE)
endif()

find_package(OpenEXR REQUIRED)
find_package(PNG REQUIRED)

# vcpkg's generated FindFFMPEG.cmake calls find_library(... NAMES ncrypt ...)
# for Windows schannel TLS support. On hosted MSVC runners the Windows SDK 'um'
# directory is not always discoverable by CMake's find_library at configure
# time, so this lookup fails with
#   "Could not find FFMPEG_DEPENDENCY_ncrypt_RELEASE".
# ncrypt.lib is a Windows system library that the MSVC linker resolves from the
# default library search path, so we pre-populate the cache with the bare name.
# find_library respects a pre-set cache entry and will not re-search. The link
# line then receives "ncrypt.lib", which the linker finds via the SDK path.
if(WIN32)
    if(NOT FFMPEG_DEPENDENCY_ncrypt_RELEASE)
        set(FFMPEG_DEPENDENCY_ncrypt_RELEASE "ncrypt"
            CACHE FILEPATH "Windows SDK ncrypt (resolved by linker)" FORCE)
    endif()
    if(NOT FFMPEG_DEPENDENCY_ncrypt_DEBUG)
        set(FFMPEG_DEPENDENCY_ncrypt_DEBUG "ncrypt"
            CACHE FILEPATH "Windows SDK ncrypt (resolved by linker)" FORCE)
    endif()
endif()

find_package(FFMPEG REQUIRED)
find_package(cxxopts REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Catch2 3 REQUIRED)

# --- Windows: app-local Visual C++ Redistributable ---
#
# Why we ship the VC++ Redist DLLs (MSVCP140.dll, VCRUNTIME140.dll, et al.)
# next to corridorkey_ofx_runtime_server.exe inside the OFX bundle:
#
# Foundry Nuke calls SetDllDirectory("C:\Program Files\Nuke<ver>") on its
# own process to pin its app-local C++ runtime. Per Microsoft's documented
# DLL search order
# (https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order),
#
#     "The standard search order of the process will also be affected by
#      calling the SetDllDirectory function in the parent process before
#      the start of the current process."
#
# That altered order places Nuke's directory ABOVE %WINDIR%\System32, so
# children spawned via CreateProcessW inherit a search where Nuke's older
# MSVCP140 wins over System32's. Issue #56 reproduced this on Nuke 17.0v2:
# the spawned sidecar imported MSVCP140 v14.36 from Nuke instead of v14.50
# from System32 and crashed at MSVCP140!0x12f58 with EXCEPTION_ACCESS_VIOLATION
# before reaching wWinMain (verified in the Application event log).
#
# The fix is the deployment Microsoft documents at
# https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files
# under "Install individual redistributable files":
#
#     "It's also possible to directly install the Redistributable DLLs in
#      the application local folder. The application local folder is the
#      folder that contains your executable application file."
#
# Search-order step #1 ("the folder from which the application loaded") is
# evaluated BEFORE any SetDllDirectory-altered step, so an app-local copy
# wins regardless of host process behavior. CMake's
# InstallRequiredSystemLibraries module discovers the redist DLLs from the
# active MSVC toolchain; src/plugins/ofx/CMakeLists.txt copies them into
# the bundle's Contents/Win64/ during the standard POST_BUILD staging.
#
# CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP=TRUE prevents the module from
# adding its own install(PROGRAMS ...) rule — we only want the discovered
# library list, not a CPack install rule (we ship through NSIS, not CPack).
if(WIN32 AND MSVC)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        message(STATUS
            "Discovered ${CMAKE_BUILD_TYPE} VC++ Redistributable DLLs for app-local "
            "bundling: ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}")
    else()
        message(WARNING
            "InstallRequiredSystemLibraries did not populate "
            "CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS; the OFX bundle will not ship "
            "the Visual C++ Redistributable app-local. Foundry Nuke hosts "
            "may load an incompatible MSVCP140.dll into the runtime server "
            "(Issue #56). Confirm a Visual Studio C++ workload is installed.")
    endif()
endif()

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

# Optional standalone tools that depend on vendored payloads. Kept under
# tools/ per ARCHITECTURE.md "Standalone auxiliary tools. Must not leak
# dependencies into the main build." Each subdirectory is self-gated; the
# torchtrt_runner only builds when the Windows TorchTRT vendor is staged.
if(CORRIDORKEY_HAS_TORCHTRT)
    add_subdirectory(tools/torchtrt_runner)
endif()

# Testing
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
