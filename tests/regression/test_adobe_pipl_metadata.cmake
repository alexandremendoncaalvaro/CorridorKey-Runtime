cmake_minimum_required(VERSION 3.28)

set(_adobe_dir "${CMAKE_CURRENT_LIST_DIR}/../../src/plugins/adobe")
set(_effect_source "${_adobe_dir}/adobe_effect.cpp")
set(_metadata_template "${_adobe_dir}/adobe_effect_metadata.hpp.in")
set(_pipl_template "${_adobe_dir}/corridorkey_adobe.r.in")

foreach(_required_file "${_effect_source}" "${_metadata_template}" "${_pipl_template}")
    if(NOT EXISTS "${_required_file}")
        message(FATAL_ERROR "Missing Adobe scaffold file: ${_required_file}")
    endif()
endforeach()

file(READ "${_metadata_template}" _metadata_text)
file(READ "${_pipl_template}" _pipl_text)
file(READ "${_effect_source}" _effect_text)

foreach(_placeholder
        CORRIDORKEY_ADOBE_EFFECT_MATCH_NAME
        CORRIDORKEY_ADOBE_GLOBAL_OUTFLAGS
        CORRIDORKEY_ADOBE_GLOBAL_OUTFLAGS2)
    if(NOT _metadata_text MATCHES "@${_placeholder}@")
        message(FATAL_ERROR
            "Metadata template does not contain @${_placeholder}@.")
    endif()
    if(NOT _pipl_text MATCHES "@${_placeholder}@")
        message(FATAL_ERROR
            "PiPL template does not contain @${_placeholder}@.")
    endif()
endforeach()

foreach(_placeholder
        CORRIDORKEY_ADOBE_EFFECT_VERSION_MAJOR
        CORRIDORKEY_ADOBE_EFFECT_VERSION_MINOR
        CORRIDORKEY_ADOBE_EFFECT_VERSION_BUG
        CORRIDORKEY_ADOBE_EFFECT_VERSION_STAGE
        CORRIDORKEY_ADOBE_EFFECT_VERSION_BUILD)
    if(NOT _metadata_text MATCHES "@${_placeholder}@")
        message(FATAL_ERROR
            "Metadata template does not contain @${_placeholder}@.")
    endif()
endforeach()

if(NOT _pipl_text MATCHES "CodeWin64X86[ \t\r\n]*\\{[ \t\r\n]*\"EffectMain\"[ \t\r\n]*\\}")
    message(FATAL_ERROR "PiPL template must declare EffectMain for Win64.")
endif()

if(NOT _pipl_text MATCHES
   "AE_Effect_Match_Name[ \t\r\n]*\\{[ \t\r\n]*\"@CORRIDORKEY_ADOBE_EFFECT_MATCH_NAME@\"[ \t\r\n]*\\}")
    message(FATAL_ERROR "PiPL template must use the generated stable match name.")
endif()

foreach(_runtime_symbol kGlobalOutFlags kGlobalOutFlags2)
    if(NOT _effect_text MATCHES "corridorkey::adobe::${_runtime_symbol}")
        message(FATAL_ERROR
            "Effect source must return ${_runtime_symbol} from generated metadata.")
    endif()
endforeach()

if(NOT _effect_text MATCHES "PF_Cmd_PARAMS_SETUP")
    message(FATAL_ERROR
        "Effect source must handle PF_Cmd_PARAMS_SETUP for parameter counts.")
endif()

foreach(_selector
        PF_Cmd_ABOUT
        PF_Cmd_GLOBAL_SETDOWN
        PF_Cmd_SEQUENCE_SETUP
        PF_Cmd_SEQUENCE_SETDOWN
        PF_Cmd_RENDER
        PF_Cmd_SMART_PRE_RENDER
        PF_Cmd_SMART_RENDER)
    if(NOT _effect_text MATCHES "${_selector}")
        message(FATAL_ERROR
            "Effect source must handle ${_selector}.")
    endif()
endforeach()

if(NOT _effect_text MATCHES "setup_effect_parameters")
    message(FATAL_ERROR
        "Effect source must delegate parameter registration to the Adobe parameter setup module.")
endif()

if(NOT _effect_text MATCHES "my_version[ \t\r\n]*=" OR
   NOT _effect_text MATCHES "PF_VERSION")
    message(FATAL_ERROR
        "Effect source must publish my_version with PF_VERSION during global setup.")
endif()
