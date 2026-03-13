# Testing suite using Catch2

function(corridorkey_stage_ort_runtime target_name)
    if(NOT WIN32)
        return()
    endif()
    if(CORRIDORKEY_ORT_RUNTIME_DIR STREQUAL "")
        return()
    endif()
    foreach(_corridorkey_dll IN ITEMS onnxruntime.dll onnxruntime_providers_shared.dll)
        set(_corridorkey_dll_path "${CORRIDORKEY_ORT_RUNTIME_DIR}/${_corridorkey_dll}")
        if(EXISTS "${_corridorkey_dll_path}")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_corridorkey_dll_path}" "$<TARGET_FILE_DIR:${target_name}>"
                COMMENT "Staging ${_corridorkey_dll} for ${target_name}"
            )
        endif()
    endforeach()
endfunction()

add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(e2e)
add_subdirectory(regression)
