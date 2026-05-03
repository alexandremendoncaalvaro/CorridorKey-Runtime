# Testing suite using Catch2

function(corridorkey_stage_ort_runtime target_name)
    if(NOT WIN32)
        return()
    endif()
    if(CORRIDORKEY_ORT_RUNTIME_DIR STREQUAL "")
        return()
    endif()
    file(GLOB _corridorkey_runtime_dlls "${CORRIDORKEY_ORT_RUNTIME_DIR}/*.dll")
    if(NOT _corridorkey_runtime_dlls)
        return()
    endif()

    # Also stage the CUDA NPP runtime DLLs that src/core/gpu_prep.cpp and
    # src/core/gpu_resize.cpp link against. The OFX bundle's POST_BUILD
    # copies them via _corridorkey_ofx_cuda_dlls (see
    # src/plugins/ofx/CMakeLists.txt); without the parallel copy here, GPU
    # unit tests pass `available()` (cudart loads from the staged ORT
    # runtime dir) but fail the first nppi* call because nppc64_12.dll et
    # al. are not findable at test runtime unless the developer's CUDA bin
    # dir happens to be on PATH. Tests must be environment-independent.
    set(_corridorkey_test_cuda_dlls)
    if(CORRIDORKEY_HAS_CUDA AND CUDAToolkit_BIN_DIR)
        foreach(_npp_name nppc64_12 nppial64_12 nppidei64_12 nppig64_12)
            set(_npp_path "${CUDAToolkit_BIN_DIR}/${_npp_name}.dll")
            if(EXISTS "${_npp_path}")
                list(APPEND _corridorkey_test_cuda_dlls "${_npp_path}")
            endif()
        endforeach()
    endif()

    # Stage the same Visual C++ Redistributable DLLs the bundle ships
    # app-local (see Issue #56 and the rationale in the root CMakeLists.txt).
    # Tests run as standalone .exe files that go through the same Win32
    # loader as the OFX runtime server; absent these DLLs in the test
    # output directory, the tests are vulnerable to whichever MSVCP140
    # the host PATH happens to resolve to and would not catch a
    # regression that, for instance, stripped the bundle staging.
    set(_corridorkey_test_vcredist_dlls)
    if(MSVC AND CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        list(APPEND _corridorkey_test_vcredist_dlls
            ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS})
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_corridorkey_runtime_dlls}
            ${_corridorkey_test_cuda_dlls}
            ${_corridorkey_test_vcredist_dlls}
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND_EXPAND_LISTS
        COMMENT "Staging ONNX Runtime / NPP / VC++ Redist DLLs for ${target_name}"
    )
endfunction()

add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(e2e)
add_subdirectory(regression)
