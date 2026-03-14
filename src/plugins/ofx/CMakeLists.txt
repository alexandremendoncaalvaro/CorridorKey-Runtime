if(NOT WIN32)
    return()
endif()

set(_corridorkey_ofx_root "${CMAKE_CURRENT_SOURCE_DIR}")
set(_corridorkey_ofx_sdk_root "${CMAKE_SOURCE_DIR}/vendor/openfx")

if(NOT EXISTS "${_corridorkey_ofx_sdk_root}/include/ofxImageEffect.h")
    message(FATAL_ERROR
        "OpenFX SDK not found at ${_corridorkey_ofx_sdk_root}. Run: git clone https://github.com/AcademySoftwareFoundation/openfx vendor/openfx")
endif()

add_library(corridorkey_ofx SHARED
    ofx_actions.cpp
    ofx_image_utils.cpp
    ofx_instance.cpp
    ofx_plugin.cpp
    ofx_render.cpp
    ofx_image_utils.hpp
    ofx_shared.hpp
)

if(WIN32)
    target_sources(corridorkey_ofx PRIVATE win_delayload_hook.cpp)
endif()

target_include_directories(corridorkey_ofx PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${_corridorkey_ofx_sdk_root}/include
)

target_link_libraries(corridorkey_ofx PRIVATE corridorkey_core)

target_compile_definitions(corridorkey_ofx PRIVATE NOMINMAX)

if(MSVC)
    target_compile_options(corridorkey_ofx PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(corridorkey_ofx PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

set_target_properties(corridorkey_ofx PROPERTIES
    OUTPUT_NAME "CorridorKey"
    SUFFIX ".ofx"
)

if(WIN32)
    target_link_libraries(corridorkey_ofx PRIVATE delayimp.lib)
    set_property(TARGET corridorkey_ofx APPEND_STRING PROPERTY LINK_FLAGS " /DELAYLOAD:onnxruntime.dll")
endif()

# Package the OFX bundle
set(_ofx_bundle_dir "${CMAKE_BINARY_DIR}/CorridorKey.ofx.bundle")

add_custom_command(TARGET corridorkey_ofx POST_BUILD
    # Create bundle directories
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_ofx_bundle_dir}/Contents/Win64"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_ofx_bundle_dir}/Contents/Resources/models"

    # Copy the plugin binary
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:corridorkey_ofx>" "${_ofx_bundle_dir}/Contents/Win64/"

    # Copy all auxiliary ONNX Runtime and TensorRT DLLs
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CORRIDORKEY_ORT_RUNTIME_DIR}" "${_ofx_bundle_dir}/Contents/Win64/"

    # Rename onnxruntime.dll to corridorkey_onnxruntime.dll to avoid conflict with Resolve's bundled ORT
    COMMAND ${CMAKE_COMMAND} -E rename "${_ofx_bundle_dir}/Contents/Win64/onnxruntime.dll" "${_ofx_bundle_dir}/Contents/Win64/corridorkey_onnxruntime.dll"

    # Copy models
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/models" "${_ofx_bundle_dir}/Contents/Resources/models"

    COMMENT "Packaging OFX bundle into ${_ofx_bundle_dir}..."
)
