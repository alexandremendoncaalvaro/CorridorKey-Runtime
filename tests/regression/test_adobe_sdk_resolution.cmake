cmake_minimum_required(VERSION 3.28)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/CorridorKeyAdobeSdk.cmake")

set(CORRIDORKEY_ENABLE_ADOBE_PLUGIN OFF)
set(CORRIDORKEY_ADOBE_SDK_ROOT "")
corridorkey_resolve_adobe_sdk()

if(CORRIDORKEY_ADOBE_PLUGIN_AVAILABLE)
    message(FATAL_ERROR "Adobe plugin must not be available when disabled.")
endif()

if(NOT CORRIDORKEY_ADOBE_PLUGIN_STATUS STREQUAL "disabled")
    message(FATAL_ERROR
        "Expected disabled status, got '${CORRIDORKEY_ADOBE_PLUGIN_STATUS}'.")
endif()

set(CORRIDORKEY_ENABLE_ADOBE_PLUGIN ON)
set(CORRIDORKEY_ADOBE_SDK_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/missing-adobe-sdk")
corridorkey_resolve_adobe_sdk()

if(CORRIDORKEY_ADOBE_PLUGIN_AVAILABLE)
    message(FATAL_ERROR "Adobe plugin must not be available without SDK files.")
endif()

if(NOT CORRIDORKEY_ADOBE_PLUGIN_STATUS STREQUAL "missing")
    message(FATAL_ERROR
        "Expected missing status, got '${CORRIDORKEY_ADOBE_PLUGIN_STATUS}'.")
endif()

set(_test_regression_dir "${CMAKE_CURRENT_LIST_DIR}")
cmake_path(ABSOLUTE_PATH _test_regression_dir
    NORMALIZE
    OUTPUT_VARIABLE _test_regression_dir)
set(_test_sdk_root "../../build/adobe-sdk-resolution")
cmake_path(ABSOLUTE_PATH _test_sdk_root
    BASE_DIRECTORY "${_test_regression_dir}"
    NORMALIZE
    OUTPUT_VARIABLE _test_sdk_root)
file(REMOVE_RECURSE "${_test_sdk_root}")

file(MAKE_DIRECTORY "${_test_sdk_root}/Headers")
file(MAKE_DIRECTORY "${_test_sdk_root}/Resources")
foreach(_header
        AEConfig.h
        AE_Effect.h
        AE_EffectCB.h
        AE_EffectVers.h
        AE_Macros.h)
    file(WRITE "${_test_sdk_root}/Headers/${_header}" "")
endforeach()
if(WIN32)
    set(_expected_pipl_tool "${_test_sdk_root}/Resources/PiPLTool.exe")
else()
    set(_expected_pipl_tool "${_test_sdk_root}/Resources/PiPLTool")
endif()
file(WRITE "${_expected_pipl_tool}" "")
file(TO_CMAKE_PATH "${_test_sdk_root}/Headers" _expected_include_dir)
file(TO_CMAKE_PATH "${_test_sdk_root}/Resources" _expected_resource_dir)
file(TO_CMAKE_PATH "${_expected_pipl_tool}" _expected_pipl_tool)

set(CORRIDORKEY_ENABLE_ADOBE_PLUGIN ON)
set(CORRIDORKEY_ADOBE_SDK_ROOT "${_test_sdk_root}")
corridorkey_resolve_adobe_sdk()

if(NOT CORRIDORKEY_ADOBE_PLUGIN_AVAILABLE)
    message(FATAL_ERROR "Adobe plugin must be available with a complete SDK.")
endif()

if(NOT CORRIDORKEY_ADOBE_PLUGIN_STATUS STREQUAL "available")
    message(FATAL_ERROR
        "Expected available status, got '${CORRIDORKEY_ADOBE_PLUGIN_STATUS}'.")
endif()

if(NOT CORRIDORKEY_ADOBE_SDK_INCLUDE_DIR STREQUAL "${_expected_include_dir}")
    message(FATAL_ERROR
        "Unexpected include dir: '${CORRIDORKEY_ADOBE_SDK_INCLUDE_DIR}'.")
endif()

if(NOT CORRIDORKEY_ADOBE_SDK_RESOURCE_DIR STREQUAL "${_expected_resource_dir}")
    message(FATAL_ERROR
        "Unexpected resource dir: '${CORRIDORKEY_ADOBE_SDK_RESOURCE_DIR}'.")
endif()

if(NOT CORRIDORKEY_ADOBE_PIPL_TOOL STREQUAL "${_expected_pipl_tool}")
    message(FATAL_ERROR
        "Unexpected PiPL tool path: '${CORRIDORKEY_ADOBE_PIPL_TOOL}'.")
endif()

file(REMOVE_RECURSE "${_test_sdk_root}")
file(MAKE_DIRECTORY "${_test_sdk_root}/Examples/Headers")
file(MAKE_DIRECTORY "${_test_sdk_root}/Examples/Resources")
foreach(_header
        AEConfig.h
        AE_Effect.h
        AE_EffectCB.h
        AE_EffectVers.h
        AE_Macros.h)
    file(WRITE "${_test_sdk_root}/Examples/Headers/${_header}" "")
endforeach()
if(WIN32)
    set(_expected_pipl_tool "${_test_sdk_root}/Examples/Resources/PiPLTool.exe")
else()
    set(_expected_pipl_tool "${_test_sdk_root}/Examples/Resources/PiPLTool")
endif()
file(WRITE "${_expected_pipl_tool}" "")
file(TO_CMAKE_PATH "${_test_sdk_root}/Examples/Headers" _expected_include_dir)
file(TO_CMAKE_PATH "${_test_sdk_root}/Examples/Resources" _expected_resource_dir)
file(TO_CMAKE_PATH "${_expected_pipl_tool}" _expected_pipl_tool)

set(CORRIDORKEY_ENABLE_ADOBE_PLUGIN ON)
set(CORRIDORKEY_ADOBE_SDK_ROOT "${_test_sdk_root}")
corridorkey_resolve_adobe_sdk()

if(NOT CORRIDORKEY_ADOBE_PLUGIN_AVAILABLE)
    message(FATAL_ERROR "Adobe plugin must support the official Examples SDK layout.")
endif()

if(NOT CORRIDORKEY_ADOBE_SDK_INCLUDE_DIR STREQUAL "${_expected_include_dir}")
    message(FATAL_ERROR
        "Unexpected Examples include dir: '${CORRIDORKEY_ADOBE_SDK_INCLUDE_DIR}'.")
endif()

if(NOT CORRIDORKEY_ADOBE_SDK_RESOURCE_DIR STREQUAL "${_expected_resource_dir}")
    message(FATAL_ERROR
        "Unexpected Examples resource dir: '${CORRIDORKEY_ADOBE_SDK_RESOURCE_DIR}'.")
endif()

if(NOT CORRIDORKEY_ADOBE_PIPL_TOOL STREQUAL "${_expected_pipl_tool}")
    message(FATAL_ERROR
        "Unexpected Examples PiPL tool path: '${CORRIDORKEY_ADOBE_PIPL_TOOL}'.")
endif()

file(REMOVE_RECURSE "${_test_sdk_root}")

cmake_path(RELATIVE_PATH _test_sdk_root
    BASE_DIRECTORY "${_test_regression_dir}/../.."
    OUTPUT_VARIABLE _test_relative_sdk_root)
file(MAKE_DIRECTORY "${_test_sdk_root}/Examples/Headers")
file(MAKE_DIRECTORY "${_test_sdk_root}/Examples/Resources")
foreach(_header
        AEConfig.h
        AE_Effect.h
        AE_EffectCB.h
        AE_EffectVers.h
        AE_Macros.h)
    file(WRITE "${_test_sdk_root}/Examples/Headers/${_header}" "")
endforeach()
if(WIN32)
    set(_expected_pipl_tool "${_test_sdk_root}/Examples/Resources/PiPLTool.exe")
else()
    set(_expected_pipl_tool "${_test_sdk_root}/Examples/Resources/PiPLTool")
endif()
file(WRITE "${_expected_pipl_tool}" "")
file(TO_CMAKE_PATH "${_test_sdk_root}/Examples/Headers" _expected_include_dir)
file(TO_CMAKE_PATH "${_test_sdk_root}/Examples/Resources" _expected_resource_dir)
file(TO_CMAKE_PATH "${_expected_pipl_tool}" _expected_pipl_tool)

set(CORRIDORKEY_ENABLE_ADOBE_PLUGIN ON)
set(CORRIDORKEY_ADOBE_SDK_ROOT "${_test_relative_sdk_root}")
corridorkey_resolve_adobe_sdk()

if(NOT CORRIDORKEY_ADOBE_PLUGIN_AVAILABLE)
    message(FATAL_ERROR
        "Adobe plugin must resolve SDK roots relative to the repository root.")
endif()

if(NOT CORRIDORKEY_ADOBE_SDK_INCLUDE_DIR STREQUAL "${_expected_include_dir}")
    message(FATAL_ERROR
        "Unexpected relative include dir: '${CORRIDORKEY_ADOBE_SDK_INCLUDE_DIR}'.")
endif()

if(NOT CORRIDORKEY_ADOBE_SDK_RESOURCE_DIR STREQUAL "${_expected_resource_dir}")
    message(FATAL_ERROR
        "Unexpected relative resource dir: '${CORRIDORKEY_ADOBE_SDK_RESOURCE_DIR}'.")
endif()

if(NOT CORRIDORKEY_ADOBE_PIPL_TOOL STREQUAL "${_expected_pipl_tool}")
    message(FATAL_ERROR
        "Unexpected relative PiPL tool path: '${CORRIDORKEY_ADOBE_PIPL_TOOL}'.")
endif()

file(REMOVE_RECURSE "${_test_sdk_root}")
file(MAKE_DIRECTORY "${_test_sdk_root}/Headers")
file(MAKE_DIRECTORY "${_test_sdk_root}/Resources")
file(WRITE "${_test_sdk_root}/Headers/AE_Effect.h" "")
if(WIN32)
    file(WRITE "${_test_sdk_root}/Resources/PiPLTool.exe" "")
else()
    file(WRITE "${_test_sdk_root}/Resources/PiPLTool" "")
endif()

set(CORRIDORKEY_ENABLE_ADOBE_PLUGIN ON)
set(CORRIDORKEY_ADOBE_SDK_ROOT "${_test_sdk_root}")
corridorkey_resolve_adobe_sdk()

if(CORRIDORKEY_ADOBE_PLUGIN_AVAILABLE)
    message(FATAL_ERROR
        "Adobe plugin must not be available with an incomplete SDK root.")
endif()

if(NOT CORRIDORKEY_ADOBE_PLUGIN_STATUS STREQUAL "missing")
    message(FATAL_ERROR
        "Expected missing status for incomplete SDK, got "
        "'${CORRIDORKEY_ADOBE_PLUGIN_STATUS}'.")
endif()

file(REMOVE_RECURSE "${_test_sdk_root}")
