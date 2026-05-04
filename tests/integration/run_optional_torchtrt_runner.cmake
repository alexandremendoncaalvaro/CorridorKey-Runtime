if(NOT DEFINED RUNNER_EXE OR RUNNER_EXE STREQUAL "")
    message(FATAL_ERROR "RUNNER_EXE is required")
endif()

if(NOT DEFINED TS_PATH OR TS_PATH STREQUAL "")
    message(FATAL_ERROR "TS_PATH is required")
endif()

if(NOT DEFINED BIN_DIR OR BIN_DIR STREQUAL "")
    message(FATAL_ERROR "BIN_DIR is required")
endif()

if(NOT EXISTS "${RUNNER_EXE}")
    message("SKIP: TorchTRT runner is not built: ${RUNNER_EXE}")
    return()
endif()

if(NOT EXISTS "${TS_PATH}")
    message("SKIP: dynamic TorchScript artifact is not staged: ${TS_PATH}")
    return()
endif()

if(NOT EXISTS "${BIN_DIR}")
    message("SKIP: TorchTRT runtime bin directory is not staged: ${BIN_DIR}")
    return()
endif()

foreach(resolution IN ITEMS 512 1024 2048)
    execute_process(
        COMMAND "${RUNNER_EXE}"
            --ts "${TS_PATH}"
            --resolution "${resolution}"
            --iterations 1
            --warmup 1
            --bin-dir "${BIN_DIR}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )

    if(result EQUAL 5)
        message("SKIP: CUDA is not available for TorchTRT runner")
        return()
    endif()

    if(NOT result EQUAL 0)
        message(STATUS "${stdout}")
        message(STATUS "${stderr}")
        message(FATAL_ERROR "TorchTRT dynamic runner failed at ${resolution}px with exit code ${result}")
    endif()

    message(STATUS "${stdout}")
endforeach()
