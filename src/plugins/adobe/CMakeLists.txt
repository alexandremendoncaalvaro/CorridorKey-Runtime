set(CORRIDORKEY_ADOBE_EFFECT_CATEGORY "CorridorKey")
set(CORRIDORKEY_ADOBE_EFFECT_SUPPORT_URL "https://github.com/alexandremendoncaalvaro/CorridorKey-Runtime/issues")
set(CORRIDORKEY_ADOBE_EFFECT_VERSION_MAJOR "1")
set(CORRIDORKEY_ADOBE_EFFECT_VERSION_MINOR "0")
set(CORRIDORKEY_ADOBE_EFFECT_VERSION_BUG "0")
set(CORRIDORKEY_ADOBE_EFFECT_VERSION_STAGE "0")
set(CORRIDORKEY_ADOBE_EFFECT_VERSION_BUILD "1")
# AE_Effect_Version must use the same bit layout as the SDK PF_VERSION macro.
math(EXPR CORRIDORKEY_ADOBE_EFFECT_VERSION
    "(((${CORRIDORKEY_ADOBE_EFFECT_VERSION_MAJOR} >> 3) & 0xf) << 26) |
     ((${CORRIDORKEY_ADOBE_EFFECT_VERSION_MAJOR} & 0x7) << 19) |
     ((${CORRIDORKEY_ADOBE_EFFECT_VERSION_MINOR} & 0xf) << 15) |
     ((${CORRIDORKEY_ADOBE_EFFECT_VERSION_BUG} & 0xf) << 11) |
     ((${CORRIDORKEY_ADOBE_EFFECT_VERSION_STAGE} & 0x3) << 9) |
     (${CORRIDORKEY_ADOBE_EFFECT_VERSION_BUILD} & 0x1ff)"
)
set(CORRIDORKEY_ADOBE_GLOBAL_OUTFLAGS "(1 << 25)")
set(CORRIDORKEY_ADOBE_GLOBAL_OUTFLAGS2 "5120")

set(_corridorkey_adobe_sources
    adobe_bridge.cpp
    adobe_bridge.hpp
    adobe_effect_render.cpp
    adobe_effect_render.hpp
    adobe_effect_runtime_request.cpp
    adobe_frame_output.cpp
    adobe_matte_params.hpp
    adobe_runtime_client_cache.cpp
    adobe_runtime_client_cache.hpp
    adobe_runtime_bridge.cpp
    adobe_sequence_state.hpp
    adobe_effect.cpp
    adobe_effect_parameters.cpp
    adobe_effect_parameters.hpp
)

function(corridorkey_add_adobe_effect target_name output_name display_name match_name node_family
         node_is_blue generated_dir)
    set(CORRIDORKEY_ADOBE_EFFECT_NAME "${display_name}")
    set(CORRIDORKEY_ADOBE_EFFECT_MATCH_NAME "${match_name}")
    set(CORRIDORKEY_ADOBE_EFFECT_NODE_FAMILY "${node_family}")
    set(CORRIDORKEY_ADOBE_EFFECT_NODE_IS_BLUE "${node_is_blue}")

    configure_file(
        adobe_effect_metadata.hpp.in
        ${generated_dir}/adobe_effect_metadata.hpp
        @ONLY
    )

    set(_corridorkey_adobe_pipl_r
        "${CMAKE_CURRENT_BINARY_DIR}/${output_name}.r")
    configure_file(
        corridorkey_adobe.r.in
        "${_corridorkey_adobe_pipl_r}"
        @ONLY
    )

    add_library(${target_name} MODULE ${_corridorkey_adobe_sources})

    set_target_properties(${target_name} PROPERTIES
        PREFIX ""
        SUFFIX ".aex"
        OUTPUT_NAME "${output_name}"
    )

    corridorkey_apply_windows_common_compile_defs(${target_name})

    target_include_directories(${target_name}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${generated_dir}
            ${PROJECT_SOURCE_DIR}/src
            ${CORRIDORKEY_ADOBE_SDK_INCLUDE_DIR}
    )

    target_link_libraries(${target_name}
        PRIVATE
            corridorkey_common
    )

    if(NOT MSVC)
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    else()
        target_compile_options(${target_name} PRIVATE /W4 /WX /permissive-)
    endif()

    if(WIN32)
        enable_language(RC)

        set(_corridorkey_adobe_pipl_rr
            "${CMAKE_CURRENT_BINARY_DIR}/${output_name}.rr")
        set(_corridorkey_adobe_pipl_rrc
            "${CMAKE_CURRENT_BINARY_DIR}/${output_name}.rrc")
        set(_corridorkey_adobe_pipl_rc
            "${CMAKE_CURRENT_BINARY_DIR}/${output_name}.rc")

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
        target_sources(${target_name} PRIVATE "${_corridorkey_adobe_pipl_rc}")
    endif()
endfunction()

corridorkey_add_adobe_effect(
    corridorkey_adobe_green
    corridorkey_adobe_green
    "CorridorKey Green v${CORRIDORKEY_VERSION_DISPLAY_STRING}"
    "com.corridorkey.effect"
    "green"
    false
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

corridorkey_add_adobe_effect(
    corridorkey_adobe_blue
    corridorkey_adobe_blue
    "CorridorKey Blue v${CORRIDORKEY_VERSION_DISPLAY_STRING}"
    "com.corridorkey.effect.blue"
    "blue"
    true
    "${CMAKE_CURRENT_BINARY_DIR}/generated_blue"
)

add_custom_target(corridorkey_adobe DEPENDS corridorkey_adobe_green corridorkey_adobe_blue)
