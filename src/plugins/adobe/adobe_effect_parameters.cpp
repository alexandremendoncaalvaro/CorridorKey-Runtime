#include "adobe_effect_parameters.hpp"

#include <array>
#include <cstdint>
#include <cstdio>

namespace {

enum class ParameterKind : std::uint8_t { Popup, FloatSlider, Checkbox };

struct ParameterDefinition {
    const char* name;
    ParameterKind kind;
    A_long disk_id;
    A_short choice_count;
    A_short default_choice;
    const char* choices;
    PF_FpShort minimum_value;
    PF_FpShort maximum_value;
    PF_FpShort default_value;
    PF_Precision precision;
    PF_Boolean default_boolean;
    const char* checkbox_label;
};

constexpr A_long kInputParameterCount = 1;
constexpr PF_ParamIndex kAppendParameter = -1;
constexpr PF_ParamFlags kPopupFlags = PF_ParamFlag_CANNOT_INTERP;
constexpr PF_ParamFlags kCheckboxFlags = PF_ParamFlag_CANNOT_INTERP;
constexpr PF_ParamFlags kSliderFlags = PF_ParamFlag_NONE;

constexpr std::array<ParameterDefinition, 12> kParameterDefinitions{{
    {"Node Identity", ParameterKind::Popup, 1001, 2, 1, "Green|Blue", 0.0, 0.0, 0.0,
     PF_Precision_INTEGER, 0, ""},
    {"Quality", ParameterKind::Popup, 1002, 5, 2,
     "Default (Draft 512)|Draft (512)|High (1024)|Ultra (1536)|Maximum (2048)", 0.0, 0.0, 0.0,
     PF_Precision_INTEGER, 0, ""},
    {"Screen Color", ParameterKind::Popup, 1003, 3, 1, "Green|Blue|Blue-Green Channel Swap", 0.0,
     0.0, 0.0, PF_Precision_INTEGER, 0, ""},
    {"Alpha Hint Policy", ParameterKind::Popup, 1004, 2, 1,
     "Auto Rough Fallback|Require External Hint", 0.0, 0.0, 0.0, PF_Precision_INTEGER, 0, ""},
    {"Despill Strength", ParameterKind::FloatSlider, 1005, 0, 0, "", 0.0, 1.0, 0.5,
     PF_Precision_HUNDREDTHS, 0, ""},
    {"Spill Method", ParameterKind::Popup, 1006, 3, 1, "Average|Double Limit|Neutral", 0.0, 0.0,
     0.0, PF_Precision_INTEGER, 0, ""},
    {"Recover Original Details", ParameterKind::Checkbox, 1007, 0, 0, "", 0.0, 0.0, 0.0,
     PF_Precision_INTEGER, 1, "Enabled"},
    {"Details Edge Shrink", ParameterKind::FloatSlider, 1008, 0, 0, "", 0.0, 100.0, 3.0,
     PF_Precision_INTEGER, 0, ""},
    {"Details Edge Feather", ParameterKind::FloatSlider, 1009, 0, 0, "", 0.0, 100.0, 7.0,
     PF_Precision_INTEGER, 0, ""},
    {"Output Mode", ParameterKind::Popup, 1010, 5, 1,
     "Processed|Matte Only|Foreground Only|Source+Matte|FG+Matte", 0.0, 0.0, 0.0,
     PF_Precision_INTEGER, 0, ""},
    {"Prepare Timeout (s)", ParameterKind::FloatSlider, 1011, 0, 0, "", 10.0, 600.0, 30.0,
     PF_Precision_INTEGER, 0, ""},
    {"Render Timeout (s)", ParameterKind::FloatSlider, 1012, 0, 0, "", 10.0, 300.0, 120.0,
     PF_Precision_INTEGER, 0, ""},
}};

static_assert(kInputParameterCount + static_cast<A_long>(kParameterDefinitions.size()) ==
              static_cast<A_long>(corridorkey::adobe::kEffectParameterSlotCount));

void copy_parameter_name(PF_ParamDef& definition, const char* name) noexcept {
    std::snprintf(definition.PF_DEF_NAME, sizeof(definition.PF_DEF_NAME), "%s", name);
}

void configure_popup(PF_ParamDef& definition, const ParameterDefinition& parameter) noexcept {
    definition.param_type = PF_Param_POPUP;
    definition.flags = kPopupFlags;
    definition.u.pd.num_choices = parameter.choice_count;
    definition.u.pd.value = parameter.default_choice;
    definition.u.pd.dephault = parameter.default_choice;
    definition.u.pd.u.PF_DEF_NAMESPTR = parameter.choices;
}

void configure_float_slider(PF_ParamDef& definition,
                            const ParameterDefinition& parameter) noexcept {
    definition.param_type = PF_Param_FLOAT_SLIDER;
    definition.flags = kSliderFlags;
    definition.u.fs_d.value = parameter.default_value;
    definition.u.fs_d.valid_min = parameter.minimum_value;
    definition.u.fs_d.valid_max = parameter.maximum_value;
    definition.u.fs_d.slider_min = parameter.minimum_value;
    definition.u.fs_d.slider_max = parameter.maximum_value;
    definition.u.fs_d.dephault = parameter.default_value;
    definition.u.fs_d.precision = parameter.precision;
}

void configure_checkbox(PF_ParamDef& definition, const ParameterDefinition& parameter) noexcept {
    definition.param_type = PF_Param_CHECKBOX;
    definition.flags = kCheckboxFlags;
    definition.u.bd.value = parameter.default_boolean;
    definition.u.bd.dephault = parameter.default_boolean;
    definition.u.bd.u.PF_DEF_NAMEPTR = parameter.checkbox_label;
}

PF_Err configure_parameter(PF_ParamDef& definition, const ParameterDefinition& parameter) noexcept {
    copy_parameter_name(definition, parameter.name);
    definition.uu.id = parameter.disk_id;

    switch (parameter.kind) {
        case ParameterKind::Popup:
            configure_popup(definition, parameter);
            return PF_Err_NONE;
        case ParameterKind::FloatSlider:
            configure_float_slider(definition, parameter);
            return PF_Err_NONE;
        case ParameterKind::Checkbox:
            configure_checkbox(definition, parameter);
            return PF_Err_NONE;
    }

    return PF_Err_BAD_CALLBACK_PARAM;
}

PF_Err add_parameter(PF_InData& input_data, const ParameterDefinition& parameter) noexcept {
    if (input_data.inter.add_param == nullptr) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    PF_ParamDef definition{};
    const PF_Err configure_status = configure_parameter(definition, parameter);
    if (configure_status != PF_Err_NONE) {
        return configure_status;
    }

    return PF_ADD_PARAM(&input_data, kAppendParameter, &definition);
}

}  // namespace

namespace corridorkey::adobe {

PF_Err setup_effect_parameters(PF_InData* input_data, PF_OutData& output_data) noexcept {
    output_data.num_params =
        kInputParameterCount + static_cast<A_long>(kParameterDefinitions.size());

    if (input_data == nullptr || input_data->inter.add_param == nullptr) {
        return PF_Err_BAD_CALLBACK_PARAM;
    }

    for (const ParameterDefinition& parameter : kParameterDefinitions) {
        const PF_Err add_status = add_parameter(*input_data, parameter);
        if (add_status != PF_Err_NONE) {
            return add_status;
        }
    }

    return PF_Err_NONE;
}

}  // namespace corridorkey::adobe
