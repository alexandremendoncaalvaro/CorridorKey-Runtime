# Standalone smoke tool that proves the Sprint 1 vendoring works on the
# operator's machine (see tools/torchtrt_runner/main.cpp). Built only when
# vendor/torchtrt-windows is staged (CORRIDORKEY_HAS_TORCHTRT set in the
# root CMakeLists). Not linked into the main runtime; no source file in
# src/ depends on this target.

add_executable(corridorkey-torchtrt-runner main.cpp)

target_compile_features(corridorkey-torchtrt-runner PRIVATE cxx_std_20)

# libtorch headers trip a lot of MSVC warnings (size_t->int narrowing,
# unreferenced enum values, deprecated GetVersionExA via boost::stacktrace).
# The project default is /W4 /WX which would block the build on these
# upstream headers. Scope the relaxation to this target only - the main
# runtime still compiles under strict warnings because none of its TUs
# pull in <torch/script.h>.
if(MSVC)
    target_compile_options(corridorkey-torchtrt-runner PRIVATE
        /bigobj
        /wd4624  # destructor was implicitly defined as deleted
        /wd4267  # conversion from size_t, possible loss of data
        /wd4244  # conversion, possible loss of data
        /wd4251  # needs to have dll-interface
        /wd4275  # non dll-interface class used as base
        /wd4996  # deprecated (GetVersionExA in boost::stacktrace)
        /EHsc
    )
    target_compile_definitions(corridorkey-torchtrt-runner PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
endif()

target_include_directories(corridorkey-torchtrt-runner SYSTEM PRIVATE
    "${CORRIDORKEY_TORCHTRT_INCLUDE_DIRS}"
    "${CORRIDORKEY_TORCHTRT_INCLUDE_DIRS}/torch/csrc/api/include"
)

target_link_libraries(corridorkey-torchtrt-runner PRIVATE
    torch::torch
    torch::torch_cpu
    torch::torch_cuda
    torch::c10
    torch::c10_cuda
    torchtrt::torchtrt
)

# CMake won't auto-copy IMPORTED SHARED DLLs to the runtime output dir.
# Copy the curated runtime payload next to the exe so dev runs work
# without the user setting PATH. Production blue-pack delivery in PR 4
# will use a different layout (the pack subdir + AddDllDirectory).
#
# Use add_custom_target ALL instead of a TARGET POST_BUILD command so
# the staging fires every build invocation, including no-op rebuilds
# after `prepare-torchtrt` updates the vendor payload without changing
# any source file. Without this, a `prepare-torchtrt` re-run + `build`
# leaves the runner exe with stale DLLs.
if(WIN32 AND DEFINED CORRIDORKEY_TORCHTRT_RUNTIME_DIR)
    add_custom_target(stage_torchtrt_runner_dlls ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CORRIDORKEY_TORCHTRT_RUNTIME_DIR}"
            "$<TARGET_FILE_DIR:corridorkey-torchtrt-runner>"
        DEPENDS corridorkey-torchtrt-runner
        COMMENT "Staging vendored TorchTRT DLLs next to corridorkey-torchtrt-runner"
        VERBATIM
    )
endif()
