# Core logic implementation files (Engine, Session, etc.)

target_sources(corridorkey_core PRIVATE
    engine.cpp
    device_detection.cpp
    gpu_prep.cpp
    gpu_resize.cpp
    inference_session.cpp
    ort_process_context.cpp
    mlx_memory_governor.cpp
    mlx_session.cpp
    mlx_probe.cpp
    windows_rtx_probe.cpp
    linux_cuda_probe.cpp
)

if(APPLE)
    # Objective-C++ probe that reads MTLDevice.recommendedMaxWorkingSetSize.
    # Kept on a dedicated .mm so the rest of the core stays pure C++20 and the
    # Metal framework link is scoped to the probe.
    target_sources(corridorkey_core PRIVATE metal_memory_probe.mm)
    target_link_libraries(corridorkey_core PRIVATE "-framework Metal" "-framework Foundation")
endif()

if(UNIX AND NOT APPLE)
    # NVML is resolved at runtime through dlopen(libnvidia-ml.so.1). We only
    # need -ldl here so dlopen/dlsym are linkable; NVML itself stays a soft
    # dependency, keeping the Linux build runnable without a GPU driver.
    target_link_libraries(corridorkey_core PRIVATE ${CMAKE_DL_LIBS})
endif()

# Strategy C, Sprint 1 PR 1 follow-up: optional TorchTRT in-process
# backend, packaged as a SHARED library so the base runtime stays
# torch-free and pays no DLL-load cost when only the green pack is
# installed.
#
# Layout:
#
#   corridorkey_torchtrt (SHARED .dll)
#     -- contains torch_trt_session.cpp
#     -- links libtorch + torchtrt + tensorrt-cu12 directly
#     -- ships inside the blue model pack at
#        <pack>/torchtrt-runtime/bin/corridorkey_torchtrt.dll
#     -- in dev builds, also POST_BUILD-copied next to torchtrt.dll in
#        vendor/torchtrt-windows/bin/ so arm_torchtrt_runtime finds it
#
#   corridorkey_core (STATIC, torch-free)
#     -- compiles torch_trt_loader.cpp (AddDllDirectory + LoadLibraryEx
#        of torchtrt.dll and corridorkey_torchtrt.dll using only Win32)
#     -- references the SHARED via /DELAYLOAD on the IMPORT lib so the
#        OS loader does not pull libtorch when no Backend::TorchTRT
#        session is created
#     -- exposes CORRIDORKEY_HAS_TORCHTRT compile define so the
#        inference_session.cpp dispatch is wired in
#
# Public header torch_trt_session.hpp stays libtorch-free; consumers of
# corridorkey_core (CLI, OFX, tests) inherit the /DELAYLOAD link
# requirement only when they reference TorchTrtSession symbols. The
# /DELAYLOAD flag itself is applied at the top-level executable /
# shared-library targets (src/cli, src/plugins/ofx, tests/integration)
# because /DELAYLOAD is a linker option and STATIC libs do not link.
if(CORRIDORKEY_HAS_TORCHTRT)
    add_library(corridorkey_torchtrt SHARED torch_trt_session.cpp)
    target_compile_features(corridorkey_torchtrt PRIVATE cxx_std_20)
    target_include_directories(corridorkey_torchtrt PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
    target_include_directories(corridorkey_torchtrt SYSTEM PRIVATE
        "${CORRIDORKEY_TORCHTRT_INCLUDE_DIRS}"
        "${CORRIDORKEY_TORCHTRT_INCLUDE_DIRS}/torch/csrc/api/include"
    )
    # Symbols cross the DLL boundary on Windows. Mark every member of
    # TorchTrtSession as exported when building this DLL and as
    # imported when consumed by corridorkey_core/cli/ofx/tests.
    target_compile_definitions(corridorkey_torchtrt
        PRIVATE
            CORRIDORKEY_TORCHTRT_BUILDING=1
        INTERFACE
            CORRIDORKEY_TORCHTRT_CONSUMING=1
    )
    target_link_libraries(corridorkey_torchtrt
        PUBLIC
            corridorkey_headers
            nlohmann_json::nlohmann_json
        PRIVATE
            torch::torch
            torch::torch_cpu
            torch::torch_cuda
            torch::c10
            torch::c10_cuda
            torchtrt::torchtrt
    )
    if(MSVC)
        target_compile_options(corridorkey_torchtrt PRIVATE
            /bigobj
            /wd4624 /wd4267 /wd4244 /wd4251 /wd4275 /wd4996
            /EHsc
        )
        target_compile_definitions(corridorkey_torchtrt PRIVATE
            NOMINMAX WIN32_LEAN_AND_MEAN
        )
    endif()

    # corridorkey_core gets the dispatch enabled and the torch-free
    # loader TU, but DOES NOT link torch directly. The dispatch in
    # inference_session.cpp references TorchTrtSession symbols, which
    # the consuming exe / OFX bundle resolves through /DELAYLOAD on
    # the corridorkey_torchtrt IMPORT lib (see add_torchtrt_delayload
    # helper applied at the top-level targets in src/cli, etc.).
    target_sources(corridorkey_core PRIVATE torch_trt_loader.cpp)
    target_link_libraries(corridorkey_core PRIVATE corridorkey_torchtrt)
    target_compile_definitions(corridorkey_core PRIVATE CORRIDORKEY_HAS_TORCHTRT=1)

    # Helper that consumers (cli/ofx/tests) call to flip the link to
    # corridorkey_torchtrt.dll into delay-loaded mode. /DELAYLOAD is a
    # linker option and STATIC libs do not link, so the static
    # corridorkey_core can declare the dependency but cannot apply the
    # flag itself.
    function(corridorkey_apply_torchtrt_delayload target)
        if(MSVC AND CORRIDORKEY_HAS_TORCHTRT)
            target_link_options(${target} PRIVATE
                /DELAYLOAD:corridorkey_torchtrt.dll
            )
            target_link_libraries(${target} PRIVATE delayimp)
        endif()
    endfunction()

    # POST_BUILD-stage corridorkey_torchtrt.dll into the curated
    # vendor/torchtrt-windows/bin/ alongside torchtrt.dll so
    # arm_torchtrt_runtime's LoadLibraryEx finds it during dev runs
    # (CLI / OFX / integration tests). Production blue-pack assembly
    # stages the same DLL under <pack>/torchtrt-runtime/bin/ via the
    # packaging pipeline.
    if(WIN32 AND DEFINED CORRIDORKEY_TORCHTRT_RUNTIME_DIR)
        add_custom_command(TARGET corridorkey_torchtrt POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:corridorkey_torchtrt>"
                "${CORRIDORKEY_TORCHTRT_RUNTIME_DIR}/$<TARGET_FILE_NAME:corridorkey_torchtrt>"
            COMMENT "Staging corridorkey_torchtrt.dll into vendor/torchtrt-windows/bin (dev only)"
            VERBATIM
        )
    endif()
endif()
