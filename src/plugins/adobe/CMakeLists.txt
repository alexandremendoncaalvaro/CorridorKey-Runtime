set(CORRIDORKEY_ADOBE_EFFECT_NAME "CorridorKey")
set(CORRIDORKEY_ADOBE_EFFECT_CATEGORY "CorridorKey")
set(CORRIDORKEY_ADOBE_EFFECT_MATCH_NAME "com.corridorkey.effect")
set(CORRIDORKEY_ADOBE_EFFECT_VERSION "1")
set(CORRIDORKEY_ADOBE_EFFECT_VERSION_MAJOR "1")
set(CORRIDORKEY_ADOBE_EFFECT_VERSION_MINOR "0")
set(CORRIDORKEY_ADOBE_EFFECT_VERSION_BUG "0")
set(CORRIDORKEY_ADOBE_EFFECT_VERSION_STAGE "0")
set(CORRIDORKEY_ADOBE_EFFECT_VERSION_BUILD "1")
set(CORRIDORKEY_ADOBE_GLOBAL_OUTFLAGS "(1 << 25)")
set(CORRIDORKEY_ADOBE_GLOBAL_OUTFLAGS2 "0")

configure_file(
    adobe_effect_metadata.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/adobe_effect_metadata.hpp
    @ONLY
)
configure_file(
    corridorkey_adobe.r.in
    ${CMAKE_CURRENT_BINARY_DIR}/corridorkey_adobe.r
    @ONLY
)

add_library(corridorkey_adobe MODULE
    adobe_bridge.cpp
    adobe_bridge.hpp
    adobe_runtime_bridge.cpp
    adobe_effect.cpp
    adobe_effect_parameters.cpp
    adobe_effect_parameters.hpp
)

set_target_properties(corridorkey_adobe PROPERTIES
    PREFIX ""
    SUFFIX ".aex"
)

corridorkey_apply_windows_common_compile_defs(corridorkey_adobe)

target_include_directories(corridorkey_adobe
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/generated
        ${PROJECT_SOURCE_DIR}/src
        ${CORRIDORKEY_ADOBE_SDK_INCLUDE_DIR}
)

target_link_libraries(corridorkey_adobe
    PRIVATE
        corridorkey_common
)

if(NOT MSVC)
    target_compile_options(corridorkey_adobe PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    target_compile_options(corridorkey_adobe PRIVATE /W4 /WX /permissive-)
endif()

if(WIN32)
    enable_language(RC)

    set(_corridorkey_adobe_pipl_r
        "${CMAKE_CURRENT_BINARY_DIR}/corridorkey_adobe.r")
    set(_corridorkey_adobe_pipl_rr
        "${CMAKE_CURRENT_BINARY_DIR}/corridorkey_adobe.rr")
    set(_corridorkey_adobe_pipl_rrc
        "${CMAKE_CURRENT_BINARY_DIR}/corridorkey_adobe.rrc")
    set(_corridorkey_adobe_pipl_rc
        "${CMAKE_CURRENT_BINARY_DIR}/corridorkey_adobe.rc")

    add_custom_command(
        OUTPUT "${_corridorkey_adobe_pipl_rc}"
        COMMAND
            ${CMAKE_COMMAND}
            -DCORRIDORKEY_ADOBE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DCORRIDORKEY_ADOBE_INCLUDE_DIR=${CORRIDORKEY_ADOBE_SDK_INCLUDE_DIR}
            -DCORRIDORKEY_ADOBE_INPUT_R=${_corridorkey_adobe_pipl_r}
            -DCORRIDORKEY_ADOBE_OUTPUT_RR=${_corridorkey_adobe_pipl_rr}
            -DCORRIDORKEY_ADOBE_OUTPUT_RRC=${_corridorkey_adobe_pipl_rrc}
            -DCORRIDORKEY_ADOBE_OUTPUT_RC=${_corridorkey_adobe_pipl_rc}
            -DCORRIDORKEY_ADOBE_PIPL_TOOL=${CORRIDORKEY_ADOBE_PIPL_TOOL}
            -P ${PROJECT_SOURCE_DIR}/cmake/CorridorKeyRunAdobePipl.cmake
        DEPENDS
            "${_corridorkey_adobe_pipl_r}"
            "${PROJECT_SOURCE_DIR}/cmake/CorridorKeyRunAdobePipl.cmake"
        VERBATIM
    )
    set_source_files_properties("${_corridorkey_adobe_pipl_rc}" PROPERTIES
        GENERATED TRUE
        LANGUAGE RC
    )
    target_sources(corridorkey_adobe PRIVATE "${_corridorkey_adobe_pipl_rc}")
endif()
