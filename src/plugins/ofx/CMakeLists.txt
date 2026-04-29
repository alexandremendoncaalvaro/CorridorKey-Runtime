set(_corridorkey_ofx_root "${CMAKE_CURRENT_SOURCE_DIR}")
set(_corridorkey_ofx_sdk_root "${CMAKE_SOURCE_DIR}/vendor/openfx")

if(NOT EXISTS "${_corridorkey_ofx_sdk_root}/include/ofxImageEffect.h")
    message(FATAL_ERROR
        "OpenFX SDK not found at ${_corridorkey_ofx_sdk_root}. Run: git clone https://github.com/AcademySoftwareFoundation/openfx vendor/openfx")
endif()

if(WIN32)
    add_library(corridorkey_ofx SHARED
        ofx_actions.cpp
        ofx_frame_cache.cpp
        ofx_frame_cache.hpp
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
        ofx_frame_cache.cpp
        ofx_frame_cache.hpp
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
    add_executable(corridorkey_ofx_runtime_server WIN32
        ofx_runtime_server_main.cpp
    )

    target_include_directories(corridorkey_ofx_runtime_server PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )

    target_link_libraries(corridorkey_ofx_runtime_server PRIVATE corridorkey_core)
    corridorkey_apply_windows_common_compile_defs(corridorkey_ofx_runtime_server)
    corridorkey_apply_windows_x64_link_dirs(corridorkey_ofx_runtime_server)

    if(MSVC)
        target_compile_options(corridorkey_ofx_runtime_server PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(corridorkey_ofx_runtime_server PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    set_target_properties(corridorkey_ofx_runtime_server PROPERTIES
        OUTPUT_NAME "corridorkey_ofx_runtime_server"
    )

    set(_corridorkey_ofx_runtime_server_runtime_dlls "")
    if(CORRIDORKEY_ORT_RUNTIME_DIR)
        file(GLOB _corridorkey_ofx_runtime_server_ort_runtime_dlls
            "${CORRIDORKEY_ORT_RUNTIME_DIR}/*.dll")
        list(APPEND _corridorkey_ofx_runtime_server_runtime_dlls
            ${_corridorkey_ofx_runtime_server_ort_runtime_dlls})
    endif()

    add_custom_command(TARGET corridorkey_ofx_runtime_server POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:corridorkey_ofx_runtime_server>
            ${_corridorkey_ofx_runtime_server_runtime_dlls}
            $<TARGET_FILE_DIR:corridorkey_ofx_runtime_server>
        COMMAND_EXPAND_LISTS
    )
endif()

target_include_directories(corridorkey_ofx PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${_corridorkey_ofx_sdk_root}/include
)

# The .ofx links exclusively against corridorkey_common (no ONNX Runtime).
# All inference work flows through corridorkey_ofx_runtime_server.exe over
# IPC, which has its own loader namespace and is therefore immune to the
# host-process CUDA-stack collision that causes ERROR_DLL_INIT_FAILED (1114)
# in Foundry Nuke. Validate via `dumpbin -imports CorridorKey.ofx` after
# every link: onnxruntime.dll must not appear in the dependents list.
target_link_libraries(corridorkey_ofx PRIVATE corridorkey_common)
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
    # The .ofx no longer references ONNX Runtime symbols directly: every
    # inference call goes through corridorkey_ofx_runtime_server.exe over
    # IPC, so the plugin DLL is intentionally ORT-import-free. Drop the
    # delay-load infrastructure (delayimp.lib + /DELAYLOAD:onnxruntime.dll)
    # along with the win_delayload_hook source. The bundle still ships
    # onnxruntime.dll for the server process, but the host's loader
    # (Foundry Nuke ships cudart64_12.dll 12.8 next to its executable)
    # cannot drag the plugin into a CUDA-version conflict because the
    # plugin import table no longer mentions onnxruntime.dll at all.
    # Validate via `dumpbin -imports CorridorKey.ofx` before each release.
    corridorkey_apply_windows_x64_link_dirs(corridorkey_ofx)
endif()

if(WIN32)
    add_dependencies(corridorkey_ofx corridorkey_cli corridorkey_ofx_runtime_server)
    # Package the OFX bundle
    set(_ofx_bundle_dir "${CMAKE_BINARY_DIR}/CorridorKey.ofx.bundle")
    file(GLOB _corridorkey_ofx_runtime_dlls
        "${CORRIDORKEY_ORT_RUNTIME_DIR}/*.dll")

    # FindCUDAToolkit maps CUDA::nppc et al. to import libs, so
    # $<TARGET_RUNTIME_DLLS:corridorkey_ofx> cannot find them. We collect
    # them explicitly from the toolkit bin dir to prevent a Windows loader
    # failure when Resolve loads the plugin on a machine without CUDA installed.
    set(_corridorkey_ofx_cuda_dlls)
    if(CORRIDORKEY_HAS_CUDA AND CUDAToolkit_BIN_DIR)
        foreach(_npp_name nppc64_12 nppial64_12 nppidei64_12 nppig64_12)
            set(_npp_path "${CUDAToolkit_BIN_DIR}/${_npp_name}.dll")
            if(EXISTS "${_npp_path}")
                list(APPEND _corridorkey_ofx_cuda_dlls "${_npp_path}")
            endif()
        endforeach()
    endif()

    if(_corridorkey_ofx_runtime_dlls)
        add_custom_command(TARGET corridorkey_ofx POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_corridorkey_ofx_runtime_dlls}
                ${_corridorkey_ofx_cuda_dlls}
                $<TARGET_RUNTIME_DLLS:corridorkey_ofx>
                "$<TARGET_FILE_DIR:corridorkey_ofx>"
            # Create bundle directories
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_ofx_bundle_dir}/Contents/Win64"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_ofx_bundle_dir}/Contents/Resources/models"

            # Copy the plugin binary
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:corridorkey_ofx>" "${_ofx_bundle_dir}/Contents/Win64/"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:corridorkey_cli>" "${_ofx_bundle_dir}/Contents/Win64/"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:corridorkey_ofx_runtime_server>" "${_ofx_bundle_dir}/Contents/Win64/"

            # Copy all auxiliary ONNX Runtime and TensorRT DLLs
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CORRIDORKEY_ORT_RUNTIME_DIR}" "${_ofx_bundle_dir}/Contents/Win64/"

            # Copy transitive runtime DLLs (e.g. libcurl/OpenSSL/zlib pulled in by
            # cpr for the in-plugin update check). Empty generator expression is
            # a no-op, so this is safe when the plugin has no dynamic deps.
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_corridorkey_ofx_cuda_dlls}
                $<TARGET_RUNTIME_DLLS:corridorkey_ofx>
                "${_ofx_bundle_dir}/Contents/Win64/"

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
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:corridorkey_ofx_runtime_server>" "${_ofx_bundle_dir}/Contents/Win64/"

            # Copy all auxiliary ONNX Runtime and TensorRT DLLs
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CORRIDORKEY_ORT_RUNTIME_DIR}" "${_ofx_bundle_dir}/Contents/Win64/"

            # Copy transitive runtime DLLs (e.g. libcurl/OpenSSL/zlib pulled in by
            # cpr for the in-plugin update check). Empty generator expression is
            # a no-op, so this is safe when the plugin has no dynamic deps.
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_corridorkey_ofx_cuda_dlls}
                $<TARGET_RUNTIME_DLLS:corridorkey_ofx>
                "${_ofx_bundle_dir}/Contents/Win64/"

            # Copy models
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/models" "${_ofx_bundle_dir}/Contents/Resources/models"

            COMMAND_EXPAND_LISTS
            COMMENT "Packaging OFX bundle into ${_ofx_bundle_dir}..."
        )
    endif()
elseif(UNIX AND NOT APPLE)
    # Linux OFX bundle layout per the OpenFX portable bundle spec. Resolve's
    # plugin loader follows the same Contents/<arch>/ convention the macOS
    # bundle uses. We co-locate the CLI (corridorkey) next to the plugin so
    # the runtime-server subcommand and doctor both resolve without PATH.
    # NOTE: add_dependencies(corridorkey_ofx corridorkey_cli) and the CLI's
    # INSTALL_RPATH are set from src/cli/CMakeLists.txt because the cli target
    # is added after src/ in the top-level CMakeLists.txt.

    # $ORIGIN keeps the loader rooted at the plugin file, so libonnxruntime.so
    # and libcorridorkey_core.so picked up from the same Contents/Linux-x86_64/
    # directory regardless of the host's /etc/ld.so.conf or LD_LIBRARY_PATH.
    set_target_properties(corridorkey_ofx PROPERTIES
        INSTALL_RPATH "$ORIGIN"
        BUILD_WITH_INSTALL_RPATH ON
    )

    set(_ofx_bundle_dir "${CMAKE_BINARY_DIR}/CorridorKey.ofx.bundle")
    set(_ofx_bundle_contents_dir "${_ofx_bundle_dir}/Contents/Linux-x86_64")

    # Shared objects shipped inside the bundle. The curated
    # onnxruntime-linux-cuda drop already contains libonnxruntime.so plus the
    # CUDA runtime .so files; packaging scripts copy the remainder of that
    # lib directory alongside the plugin.
    add_custom_command(TARGET corridorkey_ofx POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_ofx_bundle_contents_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_ofx_bundle_dir}/Contents/Resources/models"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:corridorkey_ofx>" "${_ofx_bundle_contents_dir}/"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:corridorkey_cli>" "${_ofx_bundle_contents_dir}/"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:corridorkey_core>" "${_ofx_bundle_contents_dir}/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/models" "${_ofx_bundle_dir}/Contents/Resources/models"
        COMMENT "Staging Linux OFX bundle skeleton at ${_ofx_bundle_dir}..."
    )

    if(CORRIDORKEY_ORT_RUNTIME_DIR AND EXISTS "${CORRIDORKEY_ORT_RUNTIME_DIR}")
        add_custom_command(TARGET corridorkey_ofx POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CORRIDORKEY_ORT_RUNTIME_DIR}" "${_ofx_bundle_contents_dir}/"
            COMMENT "Copying curated ONNX Runtime libraries into the Linux bundle..."
        )
    endif()

    # Stage ffmpeg .so files from vcpkg_installed (ffmpeg is built dynamic on
    # this triplet because its yasm-assembled MMX/SSE modules emit non-PIC
    # relocations that fail to link into libcorridorkey_core.so). The $ORIGIN
    # rpath on the plugin and CLI resolves them from the same bundle dir.
    set(_ofx_vcpkg_lib_dir "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib")
    if(EXISTS "${_ofx_vcpkg_lib_dir}")
        file(GLOB _ofx_vcpkg_shared_objs
            "${_ofx_vcpkg_lib_dir}/libav*.so*"
            "${_ofx_vcpkg_lib_dir}/libsw*.so*")
        if(_ofx_vcpkg_shared_objs)
            add_custom_command(TARGET corridorkey_ofx POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy ${_ofx_vcpkg_shared_objs} "${_ofx_bundle_contents_dir}/"
                COMMENT "Copying ffmpeg shared libraries into the Linux bundle..."
            )
        endif()
    endif()

    # Rewrite DT_RUNPATH on the bundle copy of libcorridorkey_core.so from the
    # absolute build-tree paths CMake bakes in at link time to $ORIGIN so the
    # loader resolves its ffmpeg / onnxruntime siblings from the bundle on any
    # end-user machine. CMake's INSTALL_RPATH=$ORIGIN is unusable here because
    # $ORIGIN is not expanded at link time, so downstream targets that link
    # against corridorkey_core could not resolve its transitive deps during
    # their own link step. patchelf rewrites the RUNPATH after all linking is
    # done, so the build-time rpath stays intact and the shipped copy is clean.
    find_program(CORRIDORKEY_PATCHELF patchelf)
    if(NOT CORRIDORKEY_PATCHELF)
        message(FATAL_ERROR
            "patchelf is required to stage the Linux OFX bundle with a portable "
            "$ORIGIN rpath on libcorridorkey_core.so. Install it with "
            "'sudo apt install patchelf' (Debian/Ubuntu) or the distro equivalent.")
    endif()
    add_custom_command(TARGET corridorkey_ofx POST_BUILD
        COMMAND ${CORRIDORKEY_PATCHELF} --set-rpath "\$ORIGIN"
            "${_ofx_bundle_contents_dir}/$<TARGET_FILE_NAME:corridorkey_core>"
        COMMENT "Rewriting libcorridorkey_core.so RUNPATH to \$ORIGIN..."
        VERBATIM
    )
endif()
