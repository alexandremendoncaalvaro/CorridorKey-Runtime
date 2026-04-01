# Testing suite using Catch2

function(corridorkey_stage_ort_runtime target_name)
    if(NOT WIN32)
        return()
    endif()
    corridorkey_stage_windows_runtime(${target_name})
endfunction()

add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(e2e)
add_subdirectory(regression)
