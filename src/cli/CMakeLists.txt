# CLI implementation
add_executable(corridorkey_cli main.cpp)

# Alias for consistent naming
set_target_properties(corridorkey_cli PROPERTIES OUTPUT_NAME corridorkey)

corridorkey_apply_windows_common_compile_defs(corridorkey_cli)

# Link against the core library
target_link_libraries(corridorkey_cli
    PRIVATE
        corridorkey_core
        cxxopts::cxxopts
        cpr::cpr
)

# Target-based compiler warnings
if(MSVC)
    target_compile_options(corridorkey_cli PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(corridorkey_cli PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

if(WIN32)
    corridorkey_apply_windows_x64_link_dirs(corridorkey_cli)
    set(_corridorkey_cli_runtime_dlls "")
    if(CORRIDORKEY_ORT_RUNTIME_DIR)
        file(GLOB _corridorkey_cli_ort_runtime_dlls
            "${CORRIDORKEY_ORT_RUNTIME_DIR}/*.dll")
        list(APPEND _corridorkey_cli_runtime_dlls ${_corridorkey_cli_ort_runtime_dlls})
    endif()

    add_custom_command(TARGET corridorkey_cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:corridorkey_core>
            $<TARGET_RUNTIME_DLLS:corridorkey_cli>
            ${_corridorkey_cli_runtime_dlls}
            $<TARGET_FILE_DIR:corridorkey_cli>
        COMMAND_EXPAND_LISTS
    )

    # Sprint 1 PR 3 dev-only: when corridorkey_core is linked against
    # libtorch (CORRIDORKEY_HAS_TORCHTRT), copy the curated TorchTRT
    # runtime payload next to the CLI exe so it can launch at all (the
    # static-link path requires torch_cpu/c10/etc to be on the loader path
    # at process start - AddDllDirectory inside main is too late).
    # Production blue-pack delivery (PR 4 follow-up) will refactor
    # torch_trt_session.cpp into a separate dynamic library that the base
    # exe loads on demand, so green-only users do not pay the ~5 GB cost.
    if(CORRIDORKEY_HAS_TORCHTRT AND DEFINED CORRIDORKEY_TORCHTRT_RUNTIME_DIR)
        add_custom_command(TARGET corridorkey_cli POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CORRIDORKEY_TORCHTRT_RUNTIME_DIR}"
                $<TARGET_FILE_DIR:corridorkey_cli>
            COMMENT "Staging vendored TorchTRT DLLs next to corridorkey.exe (dev only)"
            VERBATIM
        )
    endif()
elseif(UNIX AND NOT APPLE)
    # Make the OFX plugin wait on the CLI so the Linux bundle's POST_BUILD
    # stage (in src/plugins/ofx/CMakeLists.txt) can copy corridorkey next to
    # the plugin. Set the CLI's RPATH to $ORIGIN so that, once staged inside
    # the bundle's Contents/Linux-x86_64/, it resolves libcorridorkey_core.so
    # and libonnxruntime.so from the same directory regardless of
    # LD_LIBRARY_PATH. These statements live here (not in the ofx subdir)
    # because the cli target is added after src/ in the top-level CMakeLists.
    if(TARGET corridorkey_ofx)
        add_dependencies(corridorkey_ofx corridorkey_cli)
    endif()
    set_target_properties(corridorkey_cli PROPERTIES
        INSTALL_RPATH "$ORIGIN"
        BUILD_WITH_INSTALL_RPATH ON
    )
endif()
