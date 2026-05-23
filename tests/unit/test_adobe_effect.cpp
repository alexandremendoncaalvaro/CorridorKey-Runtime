#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "adobe_effect_metadata.hpp"
#include "plugins/adobe/adobe_effect_parameters.hpp"

extern "C" PF_Err EffectMain(PF_Cmd command, PF_InData* input_data, PF_OutData* output_data,
                             PF_ParamDef* parameters[], PF_LayerDef* output, void* extra) noexcept;

namespace {

struct CapturedAdobeParameters {
    std::vector<PF_ParamDef> definitions;
};

struct FakeAdobeHandle {
    explicit FakeAdobeHandle(std::size_t byte_count) : payload(byte_count), data(payload.data()) {}

    std::vector<std::byte> payload;
    void* data = nullptr;
    bool disposed = false;
};

struct FakeAdobeHandleHost {
    FakeAdobeHandleHost() {
        callbacks.host_new_handle = fake_new_handle;
        callbacks.host_lock_handle = fake_lock_handle;
        callbacks.host_unlock_handle = fake_unlock_handle;
        callbacks.host_dispose_handle = fake_dispose_handle;
        active_host = this;
    }

    ~FakeAdobeHandleHost() {
        active_host = nullptr;
    }

    PF_UtilCallbacks callbacks{};
    std::vector<std::unique_ptr<FakeAdobeHandle>> handles;
    int dispose_count = 0;
    int unlock_count = 0;

    PF_Handle new_handle(std::size_t byte_count) {
        auto handle = std::make_unique<FakeAdobeHandle>(byte_count);
        auto* data = &handle->data;
        handles.push_back(std::move(handle));
        return reinterpret_cast<PF_Handle>(data);
    }

    FakeAdobeHandle* record_for(PF_Handle handle) {
        for (auto& record : handles) {
            if (reinterpret_cast<PF_Handle>(&record->data) == handle) {
                return record.get();
            }
        }
        return nullptr;
    }

    static PF_Handle fake_new_handle(A_u_longlong byte_count) {
        return active_host == nullptr
                   ? nullptr
                   : active_host->new_handle(static_cast<std::size_t>(byte_count));
    }

    static void* fake_lock_handle(PF_Handle handle) {
        auto* record = active_host == nullptr ? nullptr : active_host->record_for(handle);
        return record == nullptr || record->disposed ? nullptr : record->data;
    }

    static void fake_unlock_handle(PF_Handle) {
        if (active_host != nullptr) {
            ++active_host->unlock_count;
        }
    }

    static void fake_dispose_handle(PF_Handle handle) {
        auto* record = active_host == nullptr ? nullptr : active_host->record_for(handle);
        if (record != nullptr && !record->disposed) {
            record->disposed = true;
            record->data = nullptr;
            ++active_host->dispose_count;
        }
    }

    static inline FakeAdobeHandleHost* active_host = nullptr;
};

struct FakeSmartPreRenderHost {
    FakeSmartPreRenderHost() {
        callbacks.checkout_layer = fake_checkout_layer;
        callbacks.GuidMixInPtr = fake_guid_mix_in;
        active_host = this;
    }

    ~FakeSmartPreRenderHost() {
        active_host = nullptr;
    }

    PF_PreRenderCallbacks callbacks{};
    PF_CheckoutResult checkout_result{};
    PF_RenderRequest checkout_request{};
    PF_Err checkout_status = PF_Err_NONE;
    PF_ProgPtr effect_ref = nullptr;
    PF_ParamIndex index = -1;
    A_long checkout_id = -1;
    A_long what_time = 0;
    A_long time_step = 0;
    A_u_long time_scale = 0;
    int checkout_count = 0;

    static PF_Err fake_checkout_layer(PF_ProgPtr effect_ref, PF_ParamIndex index,
                                      A_long checkout_id, const PF_RenderRequest* request,
                                      A_long what_time, A_long time_step, A_u_long time_scale,
                                      PF_CheckoutResult* checkout_result) {
        if (active_host == nullptr) {
            return PF_Err_INTERNAL_STRUCT_DAMAGED;
        }

        active_host->effect_ref = effect_ref;
        active_host->index = index;
        active_host->checkout_id = checkout_id;
        active_host->what_time = what_time;
        active_host->time_step = time_step;
        active_host->time_scale = time_scale;
        ++active_host->checkout_count;

        if (request != nullptr) {
            active_host->checkout_request = *request;
        }
        if (checkout_result != nullptr) {
            *checkout_result = active_host->checkout_result;
        }
        return active_host->checkout_status;
    }

    static PF_Err fake_guid_mix_in(PF_ProgPtr, A_u_long, const void*) {
        return PF_Err_NONE;
    }

    static inline FakeSmartPreRenderHost* active_host = nullptr;
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

TEST_CASE("After Effects sequence lifecycle owns host-managed sequence state",
          "[unit][adobe][effect][state]") {
    FakeAdobeHandleHost host;
    PF_InData input_data{};
    input_data.utils = &host.callbacks;
    PF_OutData output_data{};

    const PF_Err setup_status =
        EffectMain(PF_Cmd_SEQUENCE_SETUP, &input_data, &output_data, nullptr, nullptr, nullptr);

    REQUIRE(setup_status == PF_Err_NONE);
    REQUIRE(output_data.sequence_data != nullptr);
    CHECK(host.record_for(output_data.sequence_data) != nullptr);
    CHECK(host.unlock_count == 1);

    input_data.sequence_data = output_data.sequence_data;
    const PF_Err setdown_status =
        EffectMain(PF_Cmd_SEQUENCE_SETDOWN, &input_data, &output_data, nullptr, nullptr, nullptr);

    CHECK(setdown_status == PF_Err_NONE);
    CHECK(output_data.sequence_data == nullptr);
    CHECK(host.dispose_count == 1);
}

TEST_CASE("After Effects SmartFX pre-render checks out the source layer",
          "[unit][adobe][effect][smartfx]") {
    FakeSmartPreRenderHost host;
    host.checkout_result.result_rect = PF_LRect{.left = 10, .top = 20, .right = 110, .bottom = 120};
    host.checkout_result.max_result_rect =
        PF_LRect{.left = 0, .top = 0, .right = 1920, .bottom = 1080};

    PF_PreRenderInput pre_render_input{};
    pre_render_input.output_request.rect =
        PF_LRect{.left = 4, .top = 8, .right = 104, .bottom = 208};
    pre_render_input.output_request.channel_mask = PF_ChannelMask_ARGB;
    PF_PreRenderOutput pre_render_output{};
    PF_PreRenderExtra extra{
        .input = &pre_render_input, .output = &pre_render_output, .cb = &host.callbacks};

    PF_InData input_data{};
    input_data.effect_ref = reinterpret_cast<PF_ProgPtr>(&host);
    input_data.current_time = 42;
    input_data.time_step = 1;
    input_data.time_scale = 24;
    PF_OutData output_data{};

    const PF_Err status =
        EffectMain(PF_Cmd_SMART_PRE_RENDER, &input_data, &output_data, nullptr, nullptr, &extra);

    REQUIRE(status == PF_Err_NONE);
    CHECK(host.checkout_count == 1);
    CHECK(host.effect_ref == input_data.effect_ref);
    CHECK(host.index == corridorkey::adobe::kParamInputLayer);
    CHECK(host.checkout_id == corridorkey::adobe::kParamInputLayer);
    CHECK(host.what_time == input_data.current_time);
    CHECK(host.time_step == input_data.time_step);
    CHECK(host.time_scale == input_data.time_scale);
    CHECK(host.checkout_request.rect.left == pre_render_input.output_request.rect.left);
    CHECK(host.checkout_request.rect.top == pre_render_input.output_request.rect.top);
    CHECK(host.checkout_request.rect.right == pre_render_input.output_request.rect.right);
    CHECK(host.checkout_request.rect.bottom == pre_render_input.output_request.rect.bottom);
    CHECK(pre_render_output.result_rect.left == host.checkout_result.result_rect.left);
    CHECK(pre_render_output.result_rect.top == host.checkout_result.result_rect.top);
    CHECK(pre_render_output.result_rect.right == host.checkout_result.result_rect.right);
    CHECK(pre_render_output.result_rect.bottom == host.checkout_result.result_rect.bottom);
    CHECK(pre_render_output.max_result_rect.left == host.checkout_result.max_result_rect.left);
    CHECK(pre_render_output.max_result_rect.top == host.checkout_result.max_result_rect.top);
    CHECK(pre_render_output.max_result_rect.right == host.checkout_result.max_result_rect.right);
    CHECK(pre_render_output.max_result_rect.bottom == host.checkout_result.max_result_rect.bottom);
    CHECK(pre_render_output.pre_render_data == nullptr);
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
    CHECK(request->prepare_options.node_identity == "green");
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

TEST_CASE("After Effects blue node default drives runtime model and despill channel",
          "[unit][adobe][effect][runtime][screen-color][regression]") {
    std::array<PF_ParamDef, corridorkey::adobe::kEffectParameterSlotCount> storage{};
    std::array<PF_ParamDef*, corridorkey::adobe::kEffectParameterSlotCount> parameters{};
    for (std::size_t index = 0; index < storage.size(); ++index) {
        parameters[index] = &storage[index];
    }

    set_popup_value(storage[corridorkey::adobe::kParamNodeIdentity], 2);
    set_popup_value(storage[corridorkey::adobe::kParamQuality], 3);

    const corridorkey::adobe::AdobeEffectRuntimeRequestContext context{
        .models_root = "models",
        .host_surface = "after_effects",
        .effect_identity = corridorkey::adobe::kEffectMatchName,
        .client_instance_id = "sequence-1",
        .width = 1920,
        .height = 1080,
        .requested_device = corridorkey::DeviceInfo{"auto", 0, corridorkey::Backend::Auto},
        .engine_options = corridorkey::EngineCreateOptions{},
    };

    auto request = corridorkey::adobe::build_effect_runtime_request(parameters.data(), context);

    REQUIRE(request.has_value());
    CHECK(request->prepare_options.node_identity == "blue");
    CHECK(request->prepare_options.model_path ==
          std::filesystem::path{"models"} / "corridorkey_dynamic_blue_fp16.ts");
    CHECK(request->prepare_options.requested_device.backend == corridorkey::Backend::TorchTRT);
    CHECK(request->inference_params.despill_screen_channel == 2);
}

TEST_CASE("After Effects render parameters sanitize corrupted host slider values",
          "[unit][adobe][effect][runtime][regression]") {
    std::array<PF_ParamDef, corridorkey::adobe::kEffectParameterSlotCount> storage{};
    std::array<PF_ParamDef*, corridorkey::adobe::kEffectParameterSlotCount> parameters{};
    for (std::size_t index = 0; index < storage.size(); ++index) {
        parameters[index] = &storage[index];
    }

    set_popup_value(storage[corridorkey::adobe::kParamNodeIdentity], 2);
    set_popup_value(storage[corridorkey::adobe::kParamQuality], 3);
    set_slider_value(storage[corridorkey::adobe::kParamDespillStrength], 2.0F);
    set_slider_value(storage[corridorkey::adobe::kParamDetailsEdgeShrink], -20.0F);
    set_slider_value(storage[corridorkey::adobe::kParamDetailsEdgeFeather], 200.0F);
    set_slider_value(storage[corridorkey::adobe::kParamPrepareTimeoutSeconds], -5.0F);
    set_slider_value(storage[corridorkey::adobe::kParamRenderTimeoutSeconds],
                     std::numeric_limits<PF_FpShort>::quiet_NaN());

    const corridorkey::adobe::AdobeEffectRuntimeRequestContext context{
        .models_root = "models",
        .host_surface = "after_effects",
        .effect_identity = corridorkey::adobe::kEffectMatchName,
        .client_instance_id = "sequence-1",
        .width = 1920,
        .height = 1080,
        .requested_device = corridorkey::DeviceInfo{"auto", 0, corridorkey::Backend::Auto},
        .engine_options = corridorkey::EngineCreateOptions{},
    };

    auto request = corridorkey::adobe::build_effect_runtime_request(parameters.data(), context);

    REQUIRE(request.has_value());
    CHECK(request->prepare_options.node_identity == "blue");
    CHECK(request->prepare_options.model_path ==
          std::filesystem::path{"models"} / "corridorkey_dynamic_blue_fp16.ts");
    CHECK(request->prepare_options.requested_device.backend == corridorkey::Backend::TorchTRT);
    CHECK(request->prepare_options.prepare_timeout_ms == 10000);
    CHECK(request->inference_params.despill_strength == Catch::Approx(1.0F));
    CHECK(request->inference_params.sp_erode_px == 0);
    CHECK(request->inference_params.sp_blur_px == 100);
    CHECK(request->render_timeout_ms == 120000);
}

TEST_CASE("After Effects render parameters reject corrupted output modes",
          "[unit][adobe][effect][runtime][regression]") {
    std::array<PF_ParamDef, corridorkey::adobe::kEffectParameterSlotCount> storage{};
    std::array<PF_ParamDef*, corridorkey::adobe::kEffectParameterSlotCount> parameters{};
    for (std::size_t index = 0; index < storage.size(); ++index) {
        parameters[index] = &storage[index];
    }

    set_popup_value(storage[corridorkey::adobe::kParamQuality], 3);
    set_popup_value(storage[corridorkey::adobe::kParamOutputMode], 99);

    const corridorkey::adobe::AdobeEffectRuntimeRequestContext context{
        .models_root = "models",
        .host_surface = "after_effects",
        .effect_identity = corridorkey::adobe::kEffectMatchName,
        .client_instance_id = "sequence-1",
        .width = 1920,
        .height = 1080,
        .requested_device = corridorkey::DeviceInfo{"auto", 0, corridorkey::Backend::Auto},
        .engine_options = corridorkey::EngineCreateOptions{},
    };

    auto request = corridorkey::adobe::build_effect_runtime_request(parameters.data(), context);

    REQUIRE_FALSE(request.has_value());
    CHECK(request.error().code == corridorkey::ErrorCode::InvalidParameters);
    CHECK_THAT(request.error().message, Catch::Matchers::ContainsSubstring("output mode"));
}
