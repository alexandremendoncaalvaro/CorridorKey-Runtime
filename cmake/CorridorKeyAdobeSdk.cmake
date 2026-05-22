function(corridorkey_resolve_adobe_sdk)
    set(CORRIDORKEY_ADOBE_PLUGIN_AVAILABLE FALSE PARENT_SCOPE)
    set(CORRIDORKEY_ADOBE_PLUGIN_STATUS "missing" PARENT_SCOPE)
    set(CORRIDORKEY_ADOBE_SDK_INCLUDE_DIR "" PARENT_SCOPE)
    set(CORRIDORKEY_ADOBE_SDK_RESOURCE_DIR "" PARENT_SCOPE)
    set(CORRIDORKEY_ADOBE_PIPL_TOOL "" PARENT_SCOPE)

    if(NOT CORRIDORKEY_ENABLE_ADOBE_PLUGIN)
        set(CORRIDORKEY_ADOBE_PLUGIN_STATUS "disabled" PARENT_SCOPE)
        return()
    endif()

    if(CORRIDORKEY_ADOBE_SDK_ROOT STREQUAL "")
        return()
    endif()

    cmake_path(ABSOLUTE_PATH CORRIDORKEY_ADOBE_SDK_ROOT
        BASE_DIRECTORY "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.."
        NORMALIZE
        OUTPUT_VARIABLE _corridorkey_adobe_sdk_root)

    set(_corridorkey_adobe_candidate_roots
        "${_corridorkey_adobe_sdk_root}"
        "${_corridorkey_adobe_sdk_root}/Examples")
    set(_corridorkey_adobe_headers "")
    set(_corridorkey_adobe_resources "")
    foreach(_corridorkey_adobe_candidate_root ${_corridorkey_adobe_candidate_roots})
        set(_corridorkey_adobe_candidate_headers
            "${_corridorkey_adobe_candidate_root}/Headers")
        set(_corridorkey_adobe_candidate_resources
            "${_corridorkey_adobe_candidate_root}/Resources")
        if(EXISTS "${_corridorkey_adobe_candidate_headers}"
           AND EXISTS "${_corridorkey_adobe_candidate_resources}")
            set(_corridorkey_adobe_headers
                "${_corridorkey_adobe_candidate_headers}")
            set(_corridorkey_adobe_resources
                "${_corridorkey_adobe_candidate_resources}")
            break()
        endif()
    endforeach()

    if(_corridorkey_adobe_headers STREQUAL "")
        return()
    endif()

    foreach(_corridorkey_adobe_required_header
            AEConfig.h
            AE_Effect.h
            AE_EffectCB.h
            AE_EffectVers.h
            AE_Macros.h)
        if(NOT EXISTS
           "${_corridorkey_adobe_headers}/${_corridorkey_adobe_required_header}")
            return()
        endif()
    endforeach()

    if(WIN32)
        set(_corridorkey_adobe_pipl_tool
            "${_corridorkey_adobe_resources}/PiPLTool.exe")
    else()
        set(_corridorkey_adobe_pipl_tool
            "${_corridorkey_adobe_resources}/PiPLTool")
    endif()

    if(NOT EXISTS "${_corridorkey_adobe_pipl_tool}")
        return()
    endif()

    set(CORRIDORKEY_ADOBE_PLUGIN_AVAILABLE TRUE PARENT_SCOPE)
    set(CORRIDORKEY_ADOBE_PLUGIN_STATUS "available" PARENT_SCOPE)
    set(CORRIDORKEY_ADOBE_SDK_INCLUDE_DIR
        "${_corridorkey_adobe_headers}" PARENT_SCOPE)
    set(CORRIDORKEY_ADOBE_SDK_RESOURCE_DIR
        "${_corridorkey_adobe_resources}" PARENT_SCOPE)
    set(CORRIDORKEY_ADOBE_PIPL_TOOL
        "${_corridorkey_adobe_pipl_tool}" PARENT_SCOPE)
endfunction()
