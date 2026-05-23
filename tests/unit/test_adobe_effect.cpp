#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <filesystem>
#include <string>
#include <vector>

#include "AE_Effect.h"
#include "adobe_effect_metadata.hpp"
#include "plugins/adobe/adobe_effect_parameters.hpp"

extern "C" PF_Err EffectMain(PF_Cmd command, PF_InData* input_data, PF_OutData* output_data,
                             PF_ParamDef* parameters[], PF_LayerDef* output, void* extra) noexcept;

namespace {

struct CapturedAdobeParameters {
    std::vector<PF_ParamDef> definitions;
};

PF_Err capture_added_parameter(PF_ProgPtr effect_ref, PF_ParamIndex, PF_ParamDefPtr definition) {
    auto* captured = reinterpret_cast<CapturedAdobeParameters*>(effect_ref);
    captured->definitions.push_back(*definition);
    return PF_Err_NONE;
}

void set_popup_value(PF_ParamDef& definition, PF_ParamValue value) {
    definition.param_type = PF_Param_POPUP;
    definition.u.pd.value = value;
}

void set_slider_value(PF_ParamDef& definition, PF_FpShort value) {
    definition.param_type = PF_Param_FLOAT_SLIDER;
    definition.u.fs_d.value = value;
}

void set_checkbox_value(PF_ParamDef& definition, PF_Boolean value) {
    definition.param_type = PF_Param_CHECKBOX;
    definition.u.bd.value = value;
}

void check_popup_parameter(const PF_ParamDef& definition, A_long disk_id, const char* name,
                           const char* choices, A_short choice_count, A_short default_choice) {
    CHECK(definition.uu.id == disk_id);
    CHECK(std::string(definition.PF_DEF_NAME) == name);
    CHECK(definition.param_type == PF_Param_POPUP);
    CHECK(definition.flags == PF_ParamFlag_CANNOT_INTERP);
    CHECK(std::string(definition.u.pd.u.PF_DEF_NAMESPTR) == choices);
    CHECK(definition.u.pd.num_choices == choice_count);
    CHECK(definition.u.pd.value == default_choice);
    CHECK(definition.u.pd.dephault == default_choice);
}

void check_slider_parameter(const PF_ParamDef& definition, A_long disk_id, const char* name,
                            PF_FpShort minimum_value, PF_FpShort maximum_value,
                            PF_FpShort default_value, PF_Precision precision) {
    CHECK(definition.uu.id == disk_id);
    CHECK(std::string(definition.PF_DEF_NAME) == name);
    CHECK(definition.param_type == PF_Param_FLOAT_SLIDER);
    CHECK(definition.flags == PF_ParamFlag_NONE);
    CHECK(definition.u.fs_d.valid_min == minimum_value);
    CHECK(definition.u.fs_d.valid_max == maximum_value);
    CHECK(definition.u.fs_d.slider_min == minimum_value);
    CHECK(definition.u.fs_d.slider_max == maximum_value);
    CHECK(definition.u.fs_d.value == default_value);
    CHECK(definition.u.fs_d.dephault == default_value);
    CHECK(definition.u.fs_d.precision == precision);
}

void check_checkbox_parameter(const PF_ParamDef& definition, A_long disk_id, const char* name,
                              PF_Boolean default_value, const char* label) {
    CHECK(definition.uu.id == disk_id);
    CHECK(std::string(definition.PF_DEF_NAME) == name);
    CHECK(definition.param_type == PF_Param_CHECKBOX);
    CHECK(definition.flags == PF_ParamFlag_CANNOT_INTERP);
    CHECK(definition.u.bd.value == default_value);
    CHECK(definition.u.bd.dephault == default_value);
    CHECK(std::string(definition.u.bd.u.PF_DEF_NAMEPTR) == label);
}

}  // namespace

TEST_CASE("After Effects global setup publishes version and declared capabilities",
          "[unit][adobe][effect]") {
    PF_OutData output_data{};

    const PF_Err status =
        EffectMain(PF_Cmd_GLOBAL_SETUP, nullptr, &output_data, nullptr, nullptr, nullptr);

    CHECK(status == PF_Err_NONE);
    CHECK(output_data.my_version == PF_VERSION(corridorkey::adobe::kEffectVersionMajor,
                                               corridorkey::adobe::kEffectVersionMinor,
                                               corridorkey::adobe::kEffectVersionBug,
                                               corridorkey::adobe::kEffectVersionStage,
                                               corridorkey::adobe::kEffectVersionBuild));
    CHECK((output_data.out_flags & PF_OutFlag_DEEP_COLOR_AWARE) == PF_OutFlag_DEEP_COLOR_AWARE);
    CHECK((output_data.out_flags2 & PF_OutFlag2_SUPPORTS_SMART_RENDER) == 0);
}

TEST_CASE("After Effects dispatcher handles lifecycle selectors and rejects invalid render inputs",
          "[unit][adobe][effect]") {
    PF_OutData output_data{};

    CHECK(EffectMain(PF_Cmd_ABOUT, nullptr, &output_data, nullptr, nullptr, nullptr) ==
          PF_Err_NONE);
    CHECK_THAT(std::string(output_data.return_msg),
               Catch::Matchers::ContainsSubstring("CorridorKey"));

    CHECK(EffectMain(PF_Cmd_GLOBAL_SETDOWN, nullptr, &output_data, nullptr, nullptr, nullptr) ==
          PF_Err_NONE);
    CHECK(EffectMain(PF_Cmd_SEQUENCE_SETUP, nullptr, &output_data, nullptr, nullptr, nullptr) ==
          PF_Err_NONE);
    CHECK(EffectMain(PF_Cmd_SEQUENCE_RESETUP, nullptr, &output_data, nullptr, nullptr, nullptr) ==
          PF_Err_NONE);
    CHECK(EffectMain(PF_Cmd_SEQUENCE_SETDOWN, nullptr, &output_data, nullptr, nullptr, nullptr) ==
          PF_Err_NONE);

    CHECK(EffectMain(PF_Cmd_RENDER, nullptr, &output_data, nullptr, nullptr, nullptr) ==
          PF_Err_BAD_CALLBACK_PARAM);
    CHECK_THAT(std::string(output_data.return_msg),
               Catch::Matchers::ContainsSubstring("requires source"));

    CHECK(EffectMain(PF_Cmd_SMART_PRE_RENDER, nullptr, &output_data, nullptr, nullptr, nullptr) ==
          PF_Err_BAD_CALLBACK_PARAM);
    CHECK_THAT(std::string(output_data.return_msg), Catch::Matchers::ContainsSubstring("SmartFX"));
    CHECK(EffectMain(PF_Cmd_SMART_RENDER, nullptr, &output_data, nullptr, nullptr, nullptr) ==
          PF_Err_BAD_CALLBACK_PARAM);
}

TEST_CASE("After Effects params setup rejects missing host callbacks", "[unit][adobe][effect]") {
    PF_OutData output_data{};
    PF_InData input_data{};

    CHECK(EffectMain(PF_Cmd_PARAMS_SETUP, nullptr, &output_data, nullptr, nullptr, nullptr) ==
          PF_Err_BAD_CALLBACK_PARAM);
    CHECK(EffectMain(PF_Cmd_PARAMS_SETUP, &input_data, nullptr, nullptr, nullptr, nullptr) ==
          PF_Err_BAD_CALLBACK_PARAM);
    CHECK(EffectMain(PF_Cmd_PARAMS_SETUP, &input_data, &output_data, nullptr, nullptr, nullptr) ==
          PF_Err_BAD_CALLBACK_PARAM);
}

TEST_CASE("After Effects params setup registers CorridorKey controls", "[unit][adobe][effect]") {
    CapturedAdobeParameters captured;
    PF_InData input_data{};
    input_data.effect_ref = reinterpret_cast<PF_ProgPtr>(&captured);
    input_data.inter.add_param = capture_added_parameter;
    PF_OutData output_data{};

    const PF_Err status =
        EffectMain(PF_Cmd_PARAMS_SETUP, &input_data, &output_data, nullptr, nullptr, nullptr);

    CHECK(status == PF_Err_NONE);
    REQUIRE(captured.definitions.size() == 12);
    CHECK(output_data.num_params == static_cast<A_long>(captured.definitions.size() + 1));

    check_popup_parameter(captured.definitions[0], 1001, "Node Identity", "Green|Blue", 2, 1);
    check_popup_parameter(captured.definitions[1], 1002, "Quality",
                          "Default (Draft 512)|Draft (512)|High (1024)|Ultra (1536)|Maximum (2048)",
                          5, 2);
    check_popup_parameter(captured.definitions[2], 1003, "Screen Color",
                          "Green|Blue|Blue-Green Channel Swap", 3, 1);
    check_popup_parameter(captured.definitions[3], 1004, "Alpha Hint Policy",
                          "Auto Rough Fallback|Require External Hint", 2, 1);
    check_slider_parameter(captured.definitions[4], 1005, "Despill Strength", 0.0, 1.0, 0.5,
                           PF_Precision_HUNDREDTHS);
    check_popup_parameter(captured.definitions[5], 1006, "Spill Method",
                          "Average|Double Limit|Neutral", 3, 1);
    check_checkbox_parameter(captured.definitions[6], 1007, "Recover Original Details", 1,
                             "Enabled");
    check_slider_parameter(captured.definitions[7], 1008, "Details Edge Shrink", 0.0, 100.0, 3.0,
                           PF_Precision_INTEGER);
    check_slider_parameter(captured.definitions[8], 1009, "Details Edge Feather", 0.0, 100.0, 7.0,
                           PF_Precision_INTEGER);
    check_popup_parameter(captured.definitions[9], 1010, "Output Mode",
                          "Processed|Matte Only|Foreground Only|Source+Matte|FG+Matte", 5, 1);
    check_slider_parameter(captured.definitions[10], 1011, "Prepare Timeout (s)", 10.0, 600.0, 30.0,
                           PF_Precision_INTEGER);
    check_slider_parameter(captured.definitions[11], 1012, "Render Timeout (s)", 10.0, 300.0, 120.0,
                           PF_Precision_INTEGER);
}

TEST_CASE("After Effects render parameters build runtime request values",
          "[unit][adobe][effect][runtime]") {
    std::array<PF_ParamDef, corridorkey::adobe::kEffectParameterSlotCount> storage{};
    std::array<PF_ParamDef*, corridorkey::adobe::kEffectParameterSlotCount> parameters{};
    for (std::size_t index = 0; index < storage.size(); ++index) {
        parameters[index] = &storage[index];
    }

    set_popup_value(storage[corridorkey::adobe::kParamNodeIdentity], 1);
    set_popup_value(storage[corridorkey::adobe::kParamQuality], 3);
    set_popup_value(storage[corridorkey::adobe::kParamScreenColor], 1);
    set_popup_value(storage[corridorkey::adobe::kParamAlphaHintPolicy], 2);
    set_slider_value(storage[corridorkey::adobe::kParamDespillStrength], 0.7F);
    set_popup_value(storage[corridorkey::adobe::kParamSpillMethod], 2);
    set_checkbox_value(storage[corridorkey::adobe::kParamRecoverOriginalDetails], 1);
    set_slider_value(storage[corridorkey::adobe::kParamDetailsEdgeShrink], 5.0F);
    set_slider_value(storage[corridorkey::adobe::kParamDetailsEdgeFeather], 9.0F);
    set_popup_value(storage[corridorkey::adobe::kParamOutputMode], 2);
    set_slider_value(storage[corridorkey::adobe::kParamPrepareTimeoutSeconds], 45.0F);
    set_slider_value(storage[corridorkey::adobe::kParamRenderTimeoutSeconds], 90.0F);

    const corridorkey::adobe::AdobeEffectRuntimeRequestContext context{
        .models_root = "models",
        .host_surface = "after_effects",
        .effect_identity = corridorkey::adobe::kEffectMatchName,
        .client_instance_id = "sequence-1",
        .width = 1920,
        .height = 1080,
        .requested_device = corridorkey::DeviceInfo{"auto", 0, corridorkey::Backend::Auto},
        .engine_options = corridorkey::EngineCreateOptions{.allow_cpu_fallback = false,
                                                           .disable_cpu_ep_fallback = true},
    };

    auto request = corridorkey::adobe::build_effect_runtime_request(parameters.data(), context);

    REQUIRE(request.has_value());
    CHECK(request->prepare_options.host_surface == "after_effects");
    CHECK(request->prepare_options.effect_identity == corridorkey::adobe::kEffectMatchName);
    CHECK(request->prepare_options.client_instance_id == "sequence-1");
    CHECK(request->prepare_options.model_path ==
          std::filesystem::path{"models"} / "corridorkey_fp16_1024.onnx");
    CHECK(request->prepare_options.requested_quality_mode == 2);
    CHECK(request->prepare_options.requested_resolution == 1024);
    CHECK(request->prepare_options.effective_resolution == 1024);
    CHECK(request->prepare_options.prepare_timeout_ms == 45000);
    CHECK_FALSE(request->prepare_options.engine_options.allow_cpu_fallback);
    CHECK(request->prepare_options.engine_options.disable_cpu_ep_fallback);
    CHECK(request->inference_params.target_resolution == 1024);
    CHECK(request->inference_params.requested_quality_resolution == 1024);
    CHECK(request->inference_params.alpha_hint_policy ==
          corridorkey::AlphaHintPolicy::RequireExternalHint);
    CHECK(request->inference_params.despill_strength == Catch::Approx(0.7F));
    CHECK(request->inference_params.spill_method == 1);
    CHECK(request->inference_params.sp_erode_px == 5);
    CHECK(request->inference_params.sp_blur_px == 9);
    CHECK(request->inference_params.output_alpha_only);
    CHECK(request->output_mode == 1);
    CHECK(request->render_timeout_ms == 90000);
}
