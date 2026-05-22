foreach(_required_variable
        CORRIDORKEY_ADOBE_CXX_COMPILER
        CORRIDORKEY_ADOBE_INCLUDE_DIR
        CORRIDORKEY_ADOBE_INPUT_R
        CORRIDORKEY_ADOBE_OUTPUT_RR
        CORRIDORKEY_ADOBE_OUTPUT_RRC
        CORRIDORKEY_ADOBE_OUTPUT_RC
        CORRIDORKEY_ADOBE_PIPL_TOOL)
    if(NOT DEFINED ${_required_variable})
        message(FATAL_ERROR "Missing ${_required_variable}.")
    endif()
endforeach()

execute_process(
    COMMAND
        "${CORRIDORKEY_ADOBE_CXX_COMPILER}"
        /nologo
        /TC
        /I "${CORRIDORKEY_ADOBE_INCLUDE_DIR}"
        /EP
        "${CORRIDORKEY_ADOBE_INPUT_R}"
    OUTPUT_FILE "${CORRIDORKEY_ADOBE_OUTPUT_RR}"
    RESULT_VARIABLE _corridorkey_adobe_rr_result
    ERROR_VARIABLE _corridorkey_adobe_rr_error
)
if(NOT _corridorkey_adobe_rr_result EQUAL 0)
    message(FATAL_ERROR
        "Failed to preprocess Adobe PiPL .r file: ${_corridorkey_adobe_rr_error}")
endif()

execute_process(
    COMMAND
        "${CORRIDORKEY_ADOBE_PIPL_TOOL}"
        "${CORRIDORKEY_ADOBE_OUTPUT_RR}"
        "${CORRIDORKEY_ADOBE_OUTPUT_RRC}"
    RESULT_VARIABLE _corridorkey_adobe_rrc_result
    ERROR_VARIABLE _corridorkey_adobe_rrc_error
)
if(NOT _corridorkey_adobe_rrc_result EQUAL 0)
    message(FATAL_ERROR
        "Failed to compile Adobe PiPL resource: ${_corridorkey_adobe_rrc_error}")
endif()

execute_process(
    COMMAND
        "${CORRIDORKEY_ADOBE_CXX_COMPILER}"
        /nologo
        /TC
        /D MSWindows
        /EP
        "${CORRIDORKEY_ADOBE_OUTPUT_RRC}"
    OUTPUT_FILE "${CORRIDORKEY_ADOBE_OUTPUT_RC}"
    RESULT_VARIABLE _corridorkey_adobe_rc_result
    ERROR_VARIABLE _corridorkey_adobe_rc_error
)
if(NOT _corridorkey_adobe_rc_result EQUAL 0)
    message(FATAL_ERROR
        "Failed to preprocess Adobe PiPL .rc file: ${_corridorkey_adobe_rc_error}")
endif()
