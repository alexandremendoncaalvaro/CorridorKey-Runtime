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

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${_corridorkey_runtime_dlls}
            "$<TARGET_FILE_DIR:${target_name}>"
        COMMAND_EXPAND_LISTS
        COMMENT "Staging ONNX Runtime DLLs for ${target_name}"
    )
endfunction()

add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(e2e)
add_subdirectory(regression)
