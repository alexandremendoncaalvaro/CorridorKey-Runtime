set(_corridorkey_ofx_root "${CMAKE_CURRENT_SOURCE_DIR}")
set(_corridorkey_ofx_sdk_root "${CMAKE_SOURCE_DIR}/vendor/openfx")

if(NOT EXISTS "${_corridorkey_ofx_sdk_root}/include/ofxImageEffect.h")
    message(FATAL_ERROR
        "OpenFX SDK not found at ${_corridorkey_ofx_sdk_root}. Run: git clone https://github.com/AcademySoftwareFoundation/openfx vendor/openfx")
endif()

if(WIN32)
    add_library(corridorkey_ofx SHARED
        ofx_actions.cpp
        ofx_image_utils.cpp
        ofx_instance.cpp
        ofx_logging.cpp
        ofx_plugin.cpp
        ofx_render.cpp
        ofx_image_utils.hpp
        ofx_logging.hpp
        ofx_runtime_client.cpp
        ofx_runtime_client.hpp
        ofx_shared.hpp
    )
else()
    add_library(corridorkey_ofx MODULE
        ofx_actions.cpp
        ofx_image_utils.cpp
        ofx_instance.cpp
        ofx_logging.cpp
        ofx_plugin.cpp
        ofx_render.cpp
        ofx_image_utils.hpp
        ofx_logging.hpp
        ofx_runtime_client.cpp
        ofx_runtime_client.hpp
        ofx_shared.hpp
    )
endif()

if(WIN32)
    target_sources(corridorkey_ofx PRIVATE win_delayload_hook.cpp)
endif()

target_include_directories(corridorkey_ofx PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${_corridorkey_ofx_sdk_root}/include
)

target_link_libraries(corridorkey_ofx PRIVATE corridorkey_core)
corridorkey_apply_windows_common_compile_defs(corridorkey_ofx)

if(MSVC)
    target_compile_options(corridorkey_ofx PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(corridorkey_ofx PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

set_target_properties(corridorkey_ofx PROPERTIES
    PREFIX ""
    OUTPUT_NAME "CorridorKey"
    SUFFIX ".ofx"
)

if(WIN32)
    if(CORRIDORKEY_MSVC_DELAYIMP_LIB)
        target_link_libraries(corridorkey_ofx PRIVATE "${CORRIDORKEY_MSVC_DELAYIMP_LIB}")
    else()
        target_link_libraries(corridorkey_ofx PRIVATE delayimp.lib)
    endif()
    corridorkey_apply_windows_x64_link_dirs(corridorkey_ofx)
    set_property(TARGET corridorkey_ofx APPEND_STRING PROPERTY LINK_FLAGS " /DELAYLOAD:onnxruntime.dll")
endif()

if(WIN32)
    add_dependencies(corridorkey_ofx corridorkey_cli)
    # Package the OFX bundle
    set(_ofx_bundle_dir "${CMAKE_BINARY_DIR}/CorridorKey.ofx.bundle")
    file(GLOB _corridorkey_ofx_runtime_dlls
        "${CORRIDORKEY_ORT_RUNTIME_DIR}/*.dll")

    if(_corridorkey_ofx_runtime_dlls)
        add_custom_command(TARGET corridorkey_ofx POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_corridorkey_ofx_runtime_dlls}
                "$<TARGET_FILE_DIR:corridorkey_ofx>"
            # Create bundle directories
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_ofx_bundle_dir}/Contents/Win64"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_ofx_bundle_dir}/Contents/Resources/models"

            # Copy the plugin binary
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:corridorkey_ofx>" "${_ofx_bundle_dir}/Contents/Win64/"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:corridorkey_cli>" "${_ofx_bundle_dir}/Contents/Win64/"

            # Copy all auxiliary ONNX Runtime and TensorRT DLLs
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CORRIDORKEY_ORT_RUNTIME_DIR}" "${_ofx_bundle_dir}/Contents/Win64/"

            # Copy models
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/models" "${_ofx_bundle_dir}/Contents/Resources/models"

            COMMAND_EXPAND_LISTS
            COMMENT "Packaging OFX bundle into ${_ofx_bundle_dir}..."
        )
    else()
        add_custom_command(TARGET corridorkey_ofx POST_BUILD
            # Create bundle directories
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_ofx_bundle_dir}/Contents/Win64"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_ofx_bundle_dir}/Contents/Resources/models"

            # Copy the plugin binary
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:corridorkey_ofx>" "${_ofx_bundle_dir}/Contents/Win64/"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:corridorkey_cli>" "${_ofx_bundle_dir}/Contents/Win64/"

            # Copy all auxiliary ONNX Runtime and TensorRT DLLs
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CORRIDORKEY_ORT_RUNTIME_DIR}" "${_ofx_bundle_dir}/Contents/Win64/"

            # Copy models
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/models" "${_ofx_bundle_dir}/Contents/Resources/models"

            COMMENT "Packaging OFX bundle into ${_ofx_bundle_dir}..."
        )
    endif()
endif()
