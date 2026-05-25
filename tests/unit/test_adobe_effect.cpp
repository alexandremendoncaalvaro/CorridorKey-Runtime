#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <corridorkey/version.hpp>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectPixelFormat.h"
#include "SP/SPBasic.h"
#include "adobe_effect_metadata.hpp"
#include "plugins/adobe/adobe_effect_parameters.hpp"
#include "plugins/adobe/adobe_effect_render.hpp"
#include "plugins/adobe/adobe_runtime_client_cache.hpp"
#include "plugins/adobe/adobe_sequence_state.hpp"

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
    std::array<PF_ParamDef, corridorkey::adobe::kEffectParameterSlotCount> parameter_values{};
    PF_CheckoutResult checkout_result{};
    PF_RenderRequest checkout_request{};
    PF_Err checkout_status = PF_Err_NONE;
    PF_Err alpha_hint_checkout_status = PF_Err_NONE;
    PF_ProgPtr effect_ref = nullptr;
    PF_ParamIndex index = -1;
    PF_ParamIndex checkout_param_index = -1;
    A_long checkout_id = -1;
    A_long what_time = 0;
    A_long time_step = 0;
    A_u_long time_scale = 0;
    int checkout_count = 0;
    int checkout_param_count = 0;
    int checkin_param_count = 0;
    std::vector<PF_ParamIndex> indices;
    std::vector<A_long> checkout_ids;

    void install_parameter_callbacks(PF_InData& input_data) const noexcept {
        input_data.inter.checkout_param = fake_checkout_param;
        input_data.inter.checkin_param = fake_checkin_param;
    }

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
        active_host->indices.push_back(index);
        active_host->checkout_ids.push_back(checkout_id);
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
        if (index == corridorkey::adobe::kParamAlphaHintLayer) {
            return active_host->alpha_hint_checkout_status;
        }
        return active_host->checkout_status;
    }

    static PF_Err fake_guid_mix_in(PF_ProgPtr, A_u_long, const void*) {
        return PF_Err_NONE;
    }

    static PF_Err fake_checkout_param(PF_ProgPtr, PF_ParamIndex index, A_long, A_long, A_u_long,
                                      PF_ParamDef* parameter) {
        if (active_host == nullptr || parameter == nullptr || index < 0 ||
            static_cast<std::size_t>(index) >= active_host->parameter_values.size()) {
            return PF_Err_INVALID_INDEX;
        }

        active_host->checkout_param_index = index;
        ++active_host->checkout_param_count;
        *parameter = active_host->parameter_values[static_cast<std::size_t>(index)];
        return PF_Err_NONE;
    }

    static PF_Err fake_checkin_param(PF_ProgPtr, PF_ParamDef*) {
        if (active_host == nullptr) {
            return PF_Err_INTERNAL_STRUCT_DAMAGED;
        }

        ++active_host->checkin_param_count;
        return PF_Err_NONE;
    }

    static inline FakeSmartPreRenderHost* active_host = nullptr;
};

struct FakeSmartRenderHost {
    FakeSmartRenderHost() {
        callbacks.checkout_layer_pixels = fake_checkout_layer_pixels;
        callbacks.checkin_layer_pixels = fake_checkin_layer_pixels;
        callbacks.checkout_output = fake_checkout_output;
        active_host = this;
    }

    ~FakeSmartRenderHost() {
        active_host = nullptr;
    }

    PF_SmartRenderCallbacks callbacks{};
    std::array<PF_ParamDef, corridorkey::adobe::kEffectParameterSlotCount> parameter_values{};
    PF_EffectWorld input_world{};
    PF_EffectWorld alpha_hint_world{};
    PF_EffectWorld output_world{};
    PF_Err checkout_layer_pixels_status = PF_Err_NONE;
    PF_Err alpha_hint_checkout_layer_pixels_status = PF_Err_NONE;
    PF_Err checkout_output_status = PF_Err_NONE;
    PF_Err checkout_param_status = PF_Err_NONE;
    PF_ParamIndex checkout_param_failure_index = -1;
    PF_ProgPtr checkout_effect_ref = nullptr;
    PF_ProgPtr output_effect_ref = nullptr;
    PF_ProgPtr checkin_effect_ref = nullptr;
    A_long checkout_id = -1;
    A_long checkin_id = -1;
    std::vector<A_long> checkout_ids;
    std::vector<A_long> checkin_ids;
    int checkout_layer_pixels_count = 0;
    int checkout_output_count = 0;
    int checkin_layer_pixels_count = 0;
    int checkout_param_count = 0;
    int checkin_param_count = 0;

    void install_parameter_callbacks(PF_InData& input_data) const noexcept {
        input_data.inter.checkout_param = fake_checkout_param;
        input_data.inter.checkin_param = fake_checkin_param;
    }

    static PF_Err fake_checkout_layer_pixels(PF_ProgPtr effect_ref, A_long checkout_id,
                                             PF_EffectWorld** pixels) {
        if (active_host == nullptr) {
            return PF_Err_INTERNAL_STRUCT_DAMAGED;
        }

        active_host->checkout_effect_ref = effect_ref;
        active_host->checkout_id = checkout_id;
        active_host->checkout_ids.push_back(checkout_id);
        ++active_host->checkout_layer_pixels_count;
        if (pixels != nullptr) {
            *pixels = checkout_id == corridorkey::adobe::kParamAlphaHintLayer
                          ? &active_host->alpha_hint_world
                          : &active_host->input_world;
        }
        if (checkout_id == corridorkey::adobe::kParamAlphaHintLayer) {
            return active_host->alpha_hint_checkout_layer_pixels_status;
        }
        return active_host->checkout_layer_pixels_status;
    }

    static PF_Err fake_checkin_layer_pixels(PF_ProgPtr effect_ref, A_long checkout_id) {
        if (active_host == nullptr) {
            return PF_Err_INTERNAL_STRUCT_DAMAGED;
        }

        active_host->checkin_effect_ref = effect_ref;
        active_host->checkin_id = checkout_id;
        active_host->checkin_ids.push_back(checkout_id);
        ++active_host->checkin_layer_pixels_count;
        return PF_Err_NONE;
    }

    static PF_Err fake_checkout_output(PF_ProgPtr effect_ref, PF_EffectWorld** output) {
        if (active_host == nullptr) {
            return PF_Err_INTERNAL_STRUCT_DAMAGED;
        }

        active_host->output_effect_ref = effect_ref;
        ++active_host->checkout_output_count;
        if (output != nullptr) {
            *output = &active_host->output_world;
        }
        return active_host->checkout_output_status;
    }

    static PF_Err fake_checkout_param(PF_ProgPtr, PF_ParamIndex index, A_long, A_long, A_u_long,
                                      PF_ParamDef* parameter) {
        if (active_host == nullptr || parameter == nullptr || index < 0 ||
            static_cast<std::size_t>(index) >= active_host->parameter_values.size()) {
            return PF_Err_INVALID_INDEX;
        }

        ++active_host->checkout_param_count;
        *parameter = active_host->parameter_values[static_cast<std::size_t>(index)];
        if (index == active_host->checkout_param_failure_index) {
            return active_host->checkout_param_status;
        }
        return PF_Err_NONE;
    }

    static PF_Err fake_checkin_param(PF_ProgPtr, PF_ParamDef*) {
        if (active_host == nullptr) {
            return PF_Err_INTERNAL_STRUCT_DAMAGED;
        }

        ++active_host->checkin_param_count;
        return PF_Err_NONE;
    }

    static inline FakeSmartRenderHost* active_host = nullptr;
};

struct FakeWorldSuiteHost {
    FakeWorldSuiteHost() {
        basic.AcquireSuite = fake_acquire_suite;
        basic.ReleaseSuite = fake_release_suite;
        world_suite.PF_GetPixelFormat = fake_get_pixel_format;
        active_host = this;
    }

    ~FakeWorldSuiteHost() {
        active_host = nullptr;
    }

    SPBasicSuite basic{};
    PF_WorldSuite2 world_suite{};
    PF_PixelFormat pixel_format = PF_PixelFormat_INVALID;
    PF_Err get_pixel_format_status = PF_Err_NONE;
    int acquire_count = 0;
    int release_count = 0;
    int get_pixel_format_count = 0;

    static SPErr fake_acquire_suite(const char* name, int32 version, const void** suite) {
        if (active_host == nullptr || suite == nullptr || name == nullptr) {
            return 1;
        }
        if (std::string(name) != kPFWorldSuite || version != kPFWorldSuiteVersion2) {
            return 1;
        }

        ++active_host->acquire_count;
        *suite = &active_host->world_suite;
        return kSPNoError;
    }

    static SPErr fake_release_suite(const char* name, int32 version) {
        if (active_host == nullptr || name == nullptr) {
            return 1;
        }
        if (std::string(name) != kPFWorldSuite || version != kPFWorldSuiteVersion2) {
            return 1;
        }

        ++active_host->release_count;
        return kSPNoError;
    }

    static PF_Err fake_get_pixel_format(const PF_EffectWorld*, PF_PixelFormat* pixel_format) {
        if (active_host == nullptr || pixel_format == nullptr) {
            return PF_Err_INTERNAL_STRUCT_DAMAGED;
        }

        ++active_host->get_pixel_format_count;
        *pixel_format = active_host->pixel_format;
        return active_host->get_pixel_format_status;
    }

    static inline FakeWorldSuiteHost* active_host = nullptr;
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

void dispose_pre_render_data(PF_PreRenderOutput& output) noexcept {
    if (output.pre_render_data != nullptr && output.delete_pre_render_data_func != nullptr) {
        output.delete_pre_render_data_func(output.pre_render_data);
    }
    output.pre_render_data = nullptr;
    output.delete_pre_render_data_func = nullptr;
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

void check_layer_parameter(const PF_ParamDef& definition, A_long disk_id, const char* name,
                           A_long default_layer) {
    CHECK(definition.uu.id == disk_id);
    CHECK(std::string(definition.PF_DEF_NAME) == name);
    CHECK(definition.param_type == PF_Param_LAYER);
    CHECK(definition.flags == PF_ParamFlag_NONE);
    CHECK(definition.u.ld.dephault == default_layer);
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
    CHECK((output_data.out_flags2 & PF_OutFlag2_SUPPORTS_SMART_RENDER) ==
          PF_OutFlag2_SUPPORTS_SMART_RENDER);
    CHECK((output_data.out_flags2 & PF_OutFlag2_FLOAT_COLOR_AWARE) ==
          PF_OutFlag2_FLOAT_COLOR_AWARE);
    CHECK((output_data.out_flags2 & PF_OutFlag2_SUPPORTS_THREADED_RENDERING) == 0);
}

TEST_CASE("After Effects packaged render context resolves models beside the plugin module",
          "[unit][adobe][effect][package][regression]") {
    const auto plugin_module_path = std::filesystem::path{
        "C:/Program Files/Adobe/Common/Plug-ins/7.0/MediaCore/CorridorKey/Contents/Win64/"
        "corridorkey_adobe_green.aex"};

    const auto models_root = corridorkey::adobe::resolve_adobe_models_root(plugin_module_path);

    CHECK(models_root ==
          std::filesystem::path{
              "C:/Program Files/Adobe/Common/Plug-ins/7.0/MediaCore/CorridorKey/Contents/"
              "Resources/models"});
}

TEST_CASE("After Effects dispatcher handles lifecycle selectors and rejects invalid render inputs",
          "[unit][adobe][effect]") {
    PF_OutData output_data{};

    CHECK(EffectMain(PF_Cmd_ABOUT, nullptr, &output_data, nullptr, nullptr, nullptr) ==
          PF_Err_NONE);
    CHECK_THAT(std::string(output_data.return_msg),
               Catch::Matchers::ContainsSubstring("CorridorKey"));
    CHECK_THAT(std::string(output_data.return_msg), Catch::Matchers::ContainsSubstring("v"));
    CHECK_THAT(std::string(output_data.return_msg),
               Catch::Matchers::ContainsSubstring(CORRIDORKEY_DISPLAY_VERSION_STRING));

    CHECK(EffectMain(PF_Cmd_GLOBAL_SETDOWN, nullptr, &output_data, nullptr, nullptr, nullptr) ==
          PF_Err_NONE);
    CHECK(EffectMain(PF_Cmd_SEQUENCE_SETDOWN, nullptr, &output_data, nullptr, nullptr, nullptr) ==
          PF_Err_NONE);

    CHECK(EffectMain(PF_Cmd_RENDER, nullptr, &output_data, nullptr, nullptr, nullptr) ==
          PF_Err_BAD_CALLBACK_PARAM);
    CHECK_THAT(std::string(output_data.return_msg),
               Catch::Matchers::ContainsSubstring("requires source"));
    CHECK((output_data.out_flags & PF_OutFlag_DISPLAY_ERROR_MESSAGE) ==
          PF_OutFlag_DISPLAY_ERROR_MESSAGE);

    output_data = PF_OutData{};
    CHECK(EffectMain(PF_Cmd_SMART_PRE_RENDER, nullptr, &output_data, nullptr, nullptr, nullptr) ==
          PF_Err_BAD_CALLBACK_PARAM);
    CHECK_THAT(std::string(output_data.return_msg), Catch::Matchers::ContainsSubstring("SmartFX"));
    CHECK((output_data.out_flags & PF_OutFlag_DISPLAY_ERROR_MESSAGE) ==
          PF_OutFlag_DISPLAY_ERROR_MESSAGE);

    output_data = PF_OutData{};
    CHECK(EffectMain(PF_Cmd_SMART_RENDER, nullptr, &output_data, nullptr, nullptr, nullptr) ==
          PF_Err_BAD_CALLBACK_PARAM);
    CHECK_THAT(std::string(output_data.return_msg), Catch::Matchers::ContainsSubstring("SmartFX"));
    CHECK((output_data.out_flags & PF_OutFlag_DISPLAY_ERROR_MESSAGE) ==
          PF_OutFlag_DISPLAY_ERROR_MESSAGE);
}

TEST_CASE("After Effects sequence setup rejects missing host handle callbacks",
          "[unit][adobe][effect][state][regression]") {
    PF_InData input_data{};
    PF_OutData output_data{};

    const PF_Err status =
        EffectMain(PF_Cmd_SEQUENCE_SETUP, &input_data, &output_data, nullptr, nullptr, nullptr);

    CHECK(status == PF_Err_BAD_CALLBACK_PARAM);
    CHECK_THAT(std::string(output_data.return_msg),
               Catch::Matchers::ContainsSubstring("sequence state"));
    CHECK((output_data.out_flags & PF_OutFlag_DISPLAY_ERROR_MESSAGE) ==
          PF_OutFlag_DISPLAY_ERROR_MESSAGE);
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

    auto* state = static_cast<corridorkey::adobe::AdobeSequenceState*>(
        host.callbacks.host_lock_handle(output_data.sequence_data));
    REQUIRE(state != nullptr);
    CHECK(state->version == corridorkey::adobe::kAdobeSequenceStateVersion);
    const std::uint64_t runtime_client_key = state->runtime_client_key;
    CHECK(runtime_client_key != 0);
    host.callbacks.host_unlock_handle(output_data.sequence_data);

    corridorkey::app::HostPluginRuntimeClientOptions options;
    auto cached_client =
        corridorkey::adobe::acquire_cached_adobe_runtime_client(runtime_client_key, options);
    REQUIRE(cached_client.has_value());

    input_data.sequence_data = output_data.sequence_data;
    const PF_Err setdown_status =
        EffectMain(PF_Cmd_SEQUENCE_SETDOWN, &input_data, &output_data, nullptr, nullptr, nullptr);

    CHECK(setdown_status == PF_Err_NONE);
    CHECK(output_data.sequence_data == nullptr);
    CHECK(host.dispose_count == 1);

    auto client_after_setdown =
        corridorkey::adobe::acquire_cached_adobe_runtime_client(runtime_client_key, options);
    REQUIRE(client_after_setdown.has_value());
    CHECK(client_after_setdown->get() != cached_client->get());
    corridorkey::adobe::release_cached_adobe_runtime_client(runtime_client_key);
}

TEST_CASE("After Effects SmartFX pre-render requests optional Alpha Hint Layer",
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
    CHECK(host.checkout_count == 2);
    CHECK(host.effect_ref == input_data.effect_ref);
    REQUIRE(host.indices.size() == 2);
    REQUIRE(host.checkout_ids.size() == 2);
    CHECK(host.indices[0] == corridorkey::adobe::kParamInputLayer);
    CHECK(host.indices[1] == corridorkey::adobe::kParamAlphaHintLayer);
    CHECK(host.checkout_ids[0] == corridorkey::adobe::kParamInputLayer);
    CHECK(host.checkout_ids[1] == corridorkey::adobe::kParamAlphaHintLayer);
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
    REQUIRE(pre_render_output.pre_render_data != nullptr);
    REQUIRE(pre_render_output.delete_pre_render_data_func != nullptr);
    CHECK(host.checkout_param_count == 0);
    CHECK(host.checkin_param_count == 0);
    dispose_pre_render_data(pre_render_output);
}

TEST_CASE("After Effects SmartFX pre-render treats Alpha Hint Layer checkout failure as fallback",
          "[unit][adobe][effect][smartfx]") {
    FakeSmartPreRenderHost host;
    host.checkout_result.result_rect = PF_LRect{.left = 10, .top = 20, .right = 110, .bottom = 120};
    host.checkout_result.max_result_rect =
        PF_LRect{.left = 0, .top = 0, .right = 1920, .bottom = 1080};
    host.alpha_hint_checkout_status = PF_Err_INVALID_CALLBACK;

    PF_PreRenderInput pre_render_input{};
    pre_render_input.output_request.rect =
        PF_LRect{.left = 4, .top = 8, .right = 104, .bottom = 208};
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
    CHECK(host.checkout_count == 2);
    REQUIRE(host.indices.size() == 2);
    REQUIRE(host.checkout_ids.size() == 2);
    CHECK(host.indices[0] == corridorkey::adobe::kParamInputLayer);
    CHECK(host.indices[1] == corridorkey::adobe::kParamAlphaHintLayer);
    CHECK(host.checkout_ids[0] == corridorkey::adobe::kParamInputLayer);
    CHECK(host.checkout_ids[1] == corridorkey::adobe::kParamAlphaHintLayer);
    CHECK(host.checkout_param_count == 0);
    CHECK(host.checkin_param_count == 0);
    CHECK(pre_render_output.result_rect.left == host.checkout_result.result_rect.left);
    CHECK(pre_render_output.result_rect.top == host.checkout_result.result_rect.top);
    CHECK(pre_render_output.result_rect.right == host.checkout_result.result_rect.right);
    CHECK(pre_render_output.result_rect.bottom == host.checkout_result.result_rect.bottom);
    REQUIRE(pre_render_output.pre_render_data != nullptr);
    REQUIRE(pre_render_output.delete_pre_render_data_func != nullptr);
    dispose_pre_render_data(pre_render_output);
}

TEST_CASE("After Effects SmartFX render checks pixels back in after render failure",
          "[unit][adobe][effect][smartfx]") {
    FakeSmartRenderHost host;
    host.input_world.width = 2;
    host.input_world.height = 2;
    host.input_world.rowbytes = 8;
    host.output_world.width = 2;
    host.output_world.height = 2;
    host.output_world.rowbytes = 8;

    PF_SmartRenderInput smart_render_input{};
    PF_SmartRenderExtra extra{.input = &smart_render_input, .cb = &host.callbacks};
    PF_InData input_data{};
    input_data.effect_ref = reinterpret_cast<PF_ProgPtr>(&host);
    host.install_parameter_callbacks(input_data);
    PF_OutData output_data{};

    set_popup_value(host.parameter_values[corridorkey::adobe::kParamOutputMode], 99);

    const PF_Err status =
        EffectMain(PF_Cmd_SMART_RENDER, &input_data, &output_data, nullptr, nullptr, &extra);

    REQUIRE(status == PF_Err_BAD_CALLBACK_PARAM);
    CHECK_THAT(std::string(output_data.return_msg),
               Catch::Matchers::ContainsSubstring("output mode"));
    CHECK(host.checkout_layer_pixels_count == 1);
    CHECK(host.checkout_output_count == 1);
    CHECK(host.checkin_layer_pixels_count == 1);
    CHECK(host.checkout_effect_ref == input_data.effect_ref);
    CHECK(host.output_effect_ref == input_data.effect_ref);
    CHECK(host.checkin_effect_ref == input_data.effect_ref);
    REQUIRE(host.checkout_ids.size() == 1);
    REQUIRE(host.checkin_ids.size() == 1);
    CHECK(host.checkout_ids[0] == corridorkey::adobe::kParamInputLayer);
    CHECK(host.checkin_ids[0] == corridorkey::adobe::kParamInputLayer);
    CHECK(host.checkout_param_count ==
          static_cast<int>(corridorkey::adobe::kEffectParameterSlotCount - 2U));
    CHECK(host.checkin_param_count ==
          static_cast<int>(corridorkey::adobe::kEffectParameterSlotCount - 2U));
}

TEST_CASE(
    "After Effects SmartFX render treats optional Alpha Hint pixel checkout failure as fallback",
    "[unit][adobe][effect][smartfx]") {
    FakeSmartPreRenderHost pre_host;
    PF_PreRenderInput pre_render_input{};
    PF_PreRenderOutput pre_render_output{};
    PF_PreRenderExtra pre_render_extra{
        .input = &pre_render_input, .output = &pre_render_output, .cb = &pre_host.callbacks};
    PF_InData pre_render_in_data{};
    pre_render_in_data.effect_ref = reinterpret_cast<PF_ProgPtr>(&pre_host);
    PF_OutData pre_render_out_data{};

    const PF_Err pre_render_status =
        EffectMain(PF_Cmd_SMART_PRE_RENDER, &pre_render_in_data, &pre_render_out_data, nullptr,
                   nullptr, &pre_render_extra);

    REQUIRE(pre_render_status == PF_Err_NONE);
    REQUIRE(pre_render_output.pre_render_data != nullptr);

    FakeSmartRenderHost host;
    host.input_world.width = 2;
    host.input_world.height = 2;
    host.input_world.rowbytes = 8;
    host.alpha_hint_world.width = 2;
    host.alpha_hint_world.height = 2;
    host.alpha_hint_world.rowbytes = 8;
    host.output_world.width = 2;
    host.output_world.height = 2;
    host.output_world.rowbytes = 8;
    host.alpha_hint_checkout_layer_pixels_status = PF_Err_INVALID_CALLBACK;

    PF_SmartRenderInput smart_render_input{};
    smart_render_input.pre_render_data = pre_render_output.pre_render_data;
    PF_SmartRenderExtra extra{.input = &smart_render_input, .cb = &host.callbacks};
    PF_InData input_data{};
    input_data.effect_ref = reinterpret_cast<PF_ProgPtr>(&host);
    host.install_parameter_callbacks(input_data);
    PF_OutData output_data{};

    set_popup_value(host.parameter_values[corridorkey::adobe::kParamOutputMode], 99);

    const PF_Err status =
        EffectMain(PF_Cmd_SMART_RENDER, &input_data, &output_data, nullptr, nullptr, &extra);

    REQUIRE(status == PF_Err_BAD_CALLBACK_PARAM);
    CHECK_THAT(std::string(output_data.return_msg),
               Catch::Matchers::ContainsSubstring("output mode"));
    CHECK(host.checkout_layer_pixels_count == 2);
    CHECK(host.checkout_output_count == 1);
    CHECK(host.checkin_layer_pixels_count == 1);
    REQUIRE(host.checkout_ids.size() == 2);
    CHECK(host.checkout_ids[0] == corridorkey::adobe::kParamInputLayer);
    CHECK(host.checkout_ids[1] == corridorkey::adobe::kParamAlphaHintLayer);
    REQUIRE(host.checkin_ids.size() == 1);
    CHECK(host.checkin_ids[0] == corridorkey::adobe::kParamInputLayer);
    dispose_pre_render_data(pre_render_output);
}

TEST_CASE("After Effects SmartFX render rejects missing parameter callbacks before pixel checkout",
          "[unit][adobe][effect][smartfx]") {
    FakeSmartRenderHost host;

    PF_SmartRenderInput smart_render_input{};
    PF_SmartRenderExtra extra{.input = &smart_render_input, .cb = &host.callbacks};
    PF_InData input_data{};
    input_data.effect_ref = reinterpret_cast<PF_ProgPtr>(&host);
    PF_OutData output_data{};

    const PF_Err status =
        EffectMain(PF_Cmd_SMART_RENDER, &input_data, &output_data, nullptr, nullptr, &extra);

    REQUIRE(status == PF_Err_BAD_CALLBACK_PARAM);
    CHECK_THAT(std::string(output_data.return_msg),
               Catch::Matchers::ContainsSubstring("parameter checkout"));
    CHECK((output_data.out_flags & PF_OutFlag_DISPLAY_ERROR_MESSAGE) ==
          PF_OutFlag_DISPLAY_ERROR_MESSAGE);
    CHECK(host.checkout_param_count == 0);
    CHECK(host.checkout_layer_pixels_count == 0);
    CHECK(host.checkout_output_count == 0);
    CHECK(host.checkin_layer_pixels_count == 0);
}

TEST_CASE(
    "After Effects SmartFX render does not read Alpha Hint Layer selection via checkout_param",
    "[unit][adobe][effect][smartfx][regression]") {
    FakeSmartRenderHost host;
    host.input_world.width = 2;
    host.input_world.height = 2;
    host.input_world.rowbytes = 8;
    host.output_world.width = 2;
    host.output_world.height = 2;
    host.output_world.rowbytes = 8;
    host.checkout_param_failure_index = corridorkey::adobe::kParamAlphaHintLayer;
    host.checkout_param_status = PF_Err_INVALID_CALLBACK;

    PF_SmartRenderInput smart_render_input{};
    PF_SmartRenderExtra extra{.input = &smart_render_input, .cb = &host.callbacks};
    PF_InData input_data{};
    input_data.effect_ref = reinterpret_cast<PF_ProgPtr>(&host);
    host.install_parameter_callbacks(input_data);
    PF_OutData output_data{};

    set_popup_value(host.parameter_values[corridorkey::adobe::kParamOutputMode], 99);

    const PF_Err status =
        EffectMain(PF_Cmd_SMART_RENDER, &input_data, &output_data, nullptr, nullptr, &extra);

    REQUIRE(status == PF_Err_BAD_CALLBACK_PARAM);
    CHECK_THAT(std::string(output_data.return_msg),
               Catch::Matchers::ContainsSubstring("output mode"));
    CHECK_THAT(std::string(output_data.return_msg),
               !Catch::Matchers::ContainsSubstring("Alpha Hint Layer"));
    CHECK(host.checkout_param_count ==
          static_cast<int>(corridorkey::adobe::kEffectParameterSlotCount - 2U));
    CHECK(host.checkout_layer_pixels_count == 1);
    CHECK(host.checkout_output_count == 1);
    CHECK(host.checkin_layer_pixels_count == 1);
}

TEST_CASE(
    "After Effects SmartFX render returns host parameter checkout failures without a plugin dialog",
    "[unit][adobe][effect][smartfx]") {
    FakeSmartRenderHost host;
    host.checkout_param_failure_index = corridorkey::adobe::kParamOutputMode;
    host.checkout_param_status = PF_Err_INVALID_CALLBACK;

    PF_SmartRenderInput smart_render_input{};
    PF_SmartRenderExtra extra{.input = &smart_render_input, .cb = &host.callbacks};
    PF_InData input_data{};
    input_data.effect_ref = reinterpret_cast<PF_ProgPtr>(&host);
    host.install_parameter_callbacks(input_data);
    PF_OutData output_data{};

    const PF_Err status =
        EffectMain(PF_Cmd_SMART_RENDER, &input_data, &output_data, nullptr, nullptr, &extra);

    REQUIRE(status == PF_Err_INVALID_CALLBACK);
    CHECK(std::string(output_data.return_msg).empty());
    CHECK((output_data.out_flags & PF_OutFlag_DISPLAY_ERROR_MESSAGE) == 0);
    CHECK(host.checkout_param_count == corridorkey::adobe::kParamOutputMode - 1);
    CHECK(host.checkin_param_count == corridorkey::adobe::kParamOutputMode - 2);
    CHECK(host.checkout_layer_pixels_count == 0);
    CHECK(host.checkout_output_count == 0);
    CHECK(host.checkin_layer_pixels_count == 0);
}

TEST_CASE("After Effects SmartFX pre-render propagates source checkout cancellation silently",
          "[unit][adobe][effect][smartfx][regression]") {
    FakeSmartPreRenderHost host;
    host.checkout_status = PF_Interrupt_CANCEL;

    PF_PreRenderInput pre_render_input{};
    PF_PreRenderOutput pre_render_output{};
    PF_PreRenderExtra extra{
        .input = &pre_render_input,
        .output = &pre_render_output,
        .cb = &host.callbacks,
    };
    PF_InData input_data{};
    input_data.effect_ref = reinterpret_cast<PF_ProgPtr>(&host);
    PF_OutData output_data{};

    const PF_Err status =
        EffectMain(PF_Cmd_SMART_PRE_RENDER, &input_data, &output_data, nullptr, nullptr, &extra);

    REQUIRE(status == PF_Interrupt_CANCEL);
    CHECK(std::string(output_data.return_msg).empty());
    CHECK((output_data.out_flags & PF_OutFlag_DISPLAY_ERROR_MESSAGE) == 0);
    CHECK(host.checkout_count == 1);
    CHECK(pre_render_output.pre_render_data == nullptr);
}

TEST_CASE("After Effects SmartFX render propagates source checkout cancellation silently",
          "[unit][adobe][effect][smartfx][regression]") {
    FakeSmartRenderHost host;
    host.checkout_layer_pixels_status = PF_Interrupt_CANCEL;

    PF_SmartRenderInput smart_render_input{};
    PF_SmartRenderExtra extra{.input = &smart_render_input, .cb = &host.callbacks};
    PF_InData input_data{};
    input_data.effect_ref = reinterpret_cast<PF_ProgPtr>(&host);
    host.install_parameter_callbacks(input_data);
    PF_OutData output_data{};

    const PF_Err status =
        EffectMain(PF_Cmd_SMART_RENDER, &input_data, &output_data, nullptr, nullptr, &extra);

    REQUIRE(status == PF_Interrupt_CANCEL);
    CHECK(std::string(output_data.return_msg).empty());
    CHECK((output_data.out_flags & PF_OutFlag_DISPLAY_ERROR_MESSAGE) == 0);
    CHECK(host.checkout_layer_pixels_count == 1);
    CHECK(host.checkout_output_count == 0);
    CHECK(host.checkin_layer_pixels_count == 0);
}

TEST_CASE("After Effects SmartFX render propagates output checkout cancellation silently",
          "[unit][adobe][effect][smartfx][regression]") {
    FakeSmartRenderHost host;
    host.input_world.width = 2;
    host.input_world.height = 2;
    host.input_world.rowbytes = 8;
    host.checkout_output_status = PF_Interrupt_CANCEL;

    PF_SmartRenderInput smart_render_input{};
    PF_SmartRenderExtra extra{.input = &smart_render_input, .cb = &host.callbacks};
    PF_InData input_data{};
    input_data.effect_ref = reinterpret_cast<PF_ProgPtr>(&host);
    host.install_parameter_callbacks(input_data);
    PF_OutData output_data{};

    const PF_Err status =
        EffectMain(PF_Cmd_SMART_RENDER, &input_data, &output_data, nullptr, nullptr, &extra);

    REQUIRE(status == PF_Interrupt_CANCEL);
    CHECK(std::string(output_data.return_msg).empty());
    CHECK((output_data.out_flags & PF_OutFlag_DISPLAY_ERROR_MESSAGE) == 0);
    CHECK(host.checkout_layer_pixels_count == 1);
    CHECK(host.checkout_output_count == 1);
    CHECK(host.checkin_layer_pixels_count == 1);
}

TEST_CASE("After Effects SmartFX render treats 32 bpc worlds as ARGB128",
          "[unit][adobe][effect][smartfx][regression]") {
    FakeSmartRenderHost host;
    std::array<std::uint16_t, 8> pixels{};
    host.input_world.width = 2;
    host.input_world.height = 1;
    host.input_world.rowbytes = 16;
    host.input_world.data = reinterpret_cast<PF_PixelPtr>(pixels.data());
    host.input_world.world_flags = PF_WorldFlag_DEEP;
    host.output_world.width = 2;
    host.output_world.height = 1;
    host.output_world.rowbytes = 16;
    host.output_world.data = reinterpret_cast<PF_PixelPtr>(pixels.data());
    host.output_world.world_flags = PF_WorldFlag_DEEP;

    FakeWorldSuiteHost world_suite_host;
    world_suite_host.pixel_format = PF_PixelFormat_ARGB128;

    PF_SmartRenderInput smart_render_input{};
    smart_render_input.bitdepth = 32;
    PF_SmartRenderExtra extra{.input = &smart_render_input, .cb = &host.callbacks};
    PF_InData input_data{};
    input_data.effect_ref = reinterpret_cast<PF_ProgPtr>(&host);
    input_data.pica_basicP = &world_suite_host.basic;
    host.install_parameter_callbacks(input_data);
    PF_OutData output_data{};

    const PF_Err status =
        EffectMain(PF_Cmd_SMART_RENDER, &input_data, &output_data, nullptr, nullptr, &extra);

    REQUIRE(status == PF_Err_BAD_CALLBACK_PARAM);
    CHECK_THAT(std::string(output_data.return_msg),
               Catch::Matchers::ContainsSubstring("row bytes"));
    CHECK(world_suite_host.acquire_count == 1);
    CHECK(world_suite_host.get_pixel_format_count == 1);
    CHECK(world_suite_host.release_count == 1);
    CHECK(host.checkin_layer_pixels_count == 1);
}

TEST_CASE("After Effects SmartFX render rejects worlds without exact pixel format",
          "[unit][adobe][effect][smartfx][regression]") {
    FakeSmartRenderHost host;
    std::array<std::uint16_t, 8> pixels{};
    host.input_world.width = 2;
    host.input_world.height = 1;
    host.input_world.rowbytes = 8;
    host.input_world.data = reinterpret_cast<PF_PixelPtr>(pixels.data());
    host.input_world.world_flags = PF_WorldFlag_DEEP;
    host.output_world.width = 2;
    host.output_world.height = 1;
    host.output_world.rowbytes = 8;
    host.output_world.data = reinterpret_cast<PF_PixelPtr>(pixels.data());
    host.output_world.world_flags = PF_WorldFlag_DEEP;

    PF_SmartRenderInput smart_render_input{};
    smart_render_input.bitdepth = 32;
    PF_SmartRenderExtra extra{.input = &smart_render_input, .cb = &host.callbacks};
    PF_InData input_data{};
    input_data.effect_ref = reinterpret_cast<PF_ProgPtr>(&host);
    host.install_parameter_callbacks(input_data);
    PF_OutData output_data{};

    const PF_Err status =
        EffectMain(PF_Cmd_SMART_RENDER, &input_data, &output_data, nullptr, nullptr, &extra);

    REQUIRE(status == PF_Err_BAD_CALLBACK_PARAM);
    CHECK_THAT(std::string(output_data.return_msg),
               Catch::Matchers::ContainsSubstring("pixel format"));
    CHECK(host.checkin_layer_pixels_count == 1);
}

TEST_CASE("After Effects SmartFX render rejects failed exact pixel format lookup",
          "[unit][adobe][effect][smartfx][regression]") {
    FakeSmartRenderHost host;
    std::array<std::uint16_t, 8> pixels{};
    host.input_world.width = 2;
    host.input_world.height = 1;
    host.input_world.rowbytes = 16;
    host.input_world.data = reinterpret_cast<PF_PixelPtr>(pixels.data());
    host.input_world.world_flags = PF_WorldFlag_DEEP;
    host.output_world.width = 2;
    host.output_world.height = 1;
    host.output_world.rowbytes = 16;
    host.output_world.data = reinterpret_cast<PF_PixelPtr>(pixels.data());
    host.output_world.world_flags = PF_WorldFlag_DEEP;

    FakeWorldSuiteHost world_suite_host;
    world_suite_host.get_pixel_format_status = PF_Err_INTERNAL_STRUCT_DAMAGED;

    PF_SmartRenderInput smart_render_input{};
    smart_render_input.bitdepth = 32;
    PF_SmartRenderExtra extra{.input = &smart_render_input, .cb = &host.callbacks};
    PF_InData input_data{};
    input_data.effect_ref = reinterpret_cast<PF_ProgPtr>(&host);
    input_data.pica_basicP = &world_suite_host.basic;
    host.install_parameter_callbacks(input_data);
    PF_OutData output_data{};

    const PF_Err status =
        EffectMain(PF_Cmd_SMART_RENDER, &input_data, &output_data, nullptr, nullptr, &extra);

    REQUIRE(status == PF_Err_BAD_CALLBACK_PARAM);
    CHECK_THAT(std::string(output_data.return_msg),
               Catch::Matchers::ContainsSubstring("pixel format"));
    CHECK(world_suite_host.acquire_count == 1);
    CHECK(world_suite_host.get_pixel_format_count == 1);
    CHECK(world_suite_host.release_count == 1);
    CHECK(host.checkin_layer_pixels_count == 1);
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
    REQUIRE(captured.definitions.size() == 24);
    CHECK(output_data.num_params == static_cast<A_long>(captured.definitions.size() + 1));

    check_popup_parameter(captured.definitions[0], 1002, "Quality",
                          "Default (Draft 512)|Draft (512)|High (1024)|Ultra (1536)|Maximum (2048)",
                          5, 2);
    check_popup_parameter(captured.definitions[1], 1003, "Screen Color",
                          "Green|Blue-Green Channel Swap", 2, 1);
    check_popup_parameter(captured.definitions[2], 1026, "Input Color Space", "sRGB|Linear", 2, 1);
    check_layer_parameter(captured.definitions[3], 1013, "Alpha Hint Layer", PF_LayerDefault_NONE);
    check_slider_parameter(captured.definitions[4], 1014, "Matte Clip Black", 0.0, 1.0, 0.0,
                           PF_Precision_HUNDREDTHS);
    check_slider_parameter(captured.definitions[5], 1015, "Matte Clip White", 0.0, 1.0, 1.0,
                           PF_Precision_HUNDREDTHS);
    check_slider_parameter(captured.definitions[6], 1016, "Matte Shrink/Grow", -10.0, 10.0, 0.0,
                           PF_Precision_TENTHS);
    check_slider_parameter(captured.definitions[7], 1017, "Matte Edge Blur", 0.0, 5.0, 0.0,
                           PF_Precision_TENTHS);
    check_slider_parameter(captured.definitions[8], 1005, "Despill Strength", 0.0, 1.0, 0.5,
                           PF_Precision_HUNDREDTHS);
    check_popup_parameter(captured.definitions[9], 1006, "Spill Method",
                          "Average|Double Limit|Neutral", 3, 1);
    check_checkbox_parameter(captured.definitions[10], 1007, "Recover Original Details", 1,
                             "Enabled");
    check_slider_parameter(captured.definitions[11], 1008, "Details Edge Shrink", 0.0, 100.0, 3.0,
                           PF_Precision_INTEGER);
    check_slider_parameter(captured.definitions[12], 1009, "Details Edge Feather", 0.0, 100.0, 7.0,
                           PF_Precision_INTEGER);
    check_slider_parameter(captured.definitions[13], 1018, "Matte Gamma", 0.1F, 10.0, 1.0,
                           PF_Precision_HUNDREDTHS);
    check_checkbox_parameter(captured.definitions[14], 1019, "Auto Despeckle", 0, "Enabled");
    check_slider_parameter(captured.definitions[15], 1020, "Min Region Size", 50.0, 2000.0, 400.0,
                           PF_Precision_INTEGER);
    check_popup_parameter(captured.definitions[16], 1010, "Output Mode",
                          "Processed|Matte Only|Foreground Only|Source+Matte|FG+Matte", 5, 1);
    check_checkbox_parameter(captured.definitions[17], 1021, "Enable Tiling", 0, "Enabled");
    check_slider_parameter(captured.definitions[18], 1022, "Tile Overlap", 8.0, 128.0, 64.0,
                           PF_Precision_INTEGER);
    check_popup_parameter(captured.definitions[19], 1023, "Upscale Method", "Lanczos4|Bilinear", 2,
                          2);
    check_popup_parameter(captured.definitions[20], 1024, "Quality Fallback",
                          "Default (Direct)|Direct|Coarse to Fine", 3, 1);
    check_popup_parameter(captured.definitions[21], 1025, "Coarse Resolution Override",
                          "Recommended|512|1024|1536|2048", 5, 1);
    check_slider_parameter(captured.definitions[22], 1011, "Prepare Timeout (s)", 10.0, 600.0, 30.0,
                           PF_Precision_INTEGER);
    check_slider_parameter(captured.definitions[23], 1012, "Render Timeout (s)", 10.0, 300.0, 120.0,
                           PF_Precision_INTEGER);
}

TEST_CASE("After Effects render parameters build runtime request values",
          "[unit][adobe][effect][runtime]") {
    std::array<PF_ParamDef, corridorkey::adobe::kEffectParameterSlotCount> storage{};
    std::array<PF_ParamDef*, corridorkey::adobe::kEffectParameterSlotCount> parameters{};
    for (std::size_t index = 0; index < storage.size(); ++index) {
        parameters[index] = &storage[index];
    }

    set_popup_value(storage[corridorkey::adobe::kParamQuality], 3);
    set_popup_value(storage[corridorkey::adobe::kParamScreenColor], 1);
    set_slider_value(storage[corridorkey::adobe::kParamMatteClipBlack], 0.2F);
    set_slider_value(storage[corridorkey::adobe::kParamMatteClipWhite], 0.8F);
    set_slider_value(storage[corridorkey::adobe::kParamMatteShrinkGrow], -2.0F);
    set_slider_value(storage[corridorkey::adobe::kParamMatteEdgeBlur], 1.5F);
    set_slider_value(storage[corridorkey::adobe::kParamDespillStrength], 0.7F);
    set_popup_value(storage[corridorkey::adobe::kParamSpillMethod], 2);
    set_checkbox_value(storage[corridorkey::adobe::kParamRecoverOriginalDetails], 1);
    set_slider_value(storage[corridorkey::adobe::kParamDetailsEdgeShrink], 5.0F);
    set_slider_value(storage[corridorkey::adobe::kParamDetailsEdgeFeather], 9.0F);
    set_slider_value(storage[corridorkey::adobe::kParamMatteGamma], 1.3F);
    set_checkbox_value(storage[corridorkey::adobe::kParamAutoDespeckle], 1);
    set_slider_value(storage[corridorkey::adobe::kParamDespeckleSize], 750.0F);
    set_popup_value(storage[corridorkey::adobe::kParamOutputMode], 2);
    set_checkbox_value(storage[corridorkey::adobe::kParamEnableTiling], 1);
    set_slider_value(storage[corridorkey::adobe::kParamTileOverlap], 96.0F);
    set_popup_value(storage[corridorkey::adobe::kParamUpscaleMethod], 1);
    set_popup_value(storage[corridorkey::adobe::kParamQualityFallbackMode], 1);
    set_popup_value(storage[corridorkey::adobe::kParamCoarseResolutionOverride], 3);
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
          corridorkey::AlphaHintPolicy::AutoRoughFallback);
    CHECK_FALSE(request->inference_params.input_is_linear);
    CHECK(request->matte_params.black_point == Catch::Approx(0.2));
    CHECK(request->matte_params.white_point == Catch::Approx(0.8));
    CHECK(request->matte_params.shrink_grow_pixels == Catch::Approx(-2.0));
    CHECK(request->matte_params.edge_blur_pixels == Catch::Approx(1.5));
    CHECK(request->matte_params.gamma == Catch::Approx(1.3));
    CHECK(request->inference_params.despill_strength == Catch::Approx(0.7F));
    CHECK(request->inference_params.spill_method == 1);
    CHECK(request->inference_params.auto_despeckle);
    CHECK(request->inference_params.despeckle_size == 750);
    CHECK(request->inference_params.enable_tiling);
    CHECK(request->inference_params.tile_padding == 96);
    CHECK(request->inference_params.upscale_method == corridorkey::UpscaleMethod::Lanczos4);
    CHECK(request->inference_params.quality_fallback_mode ==
          corridorkey::QualityFallbackMode::Direct);
    CHECK(request->inference_params.coarse_resolution_override == 0);
    CHECK(request->inference_params.sp_erode_px == 5);
    CHECK(request->inference_params.sp_blur_px == 9);
    CHECK(request->inference_params.output_alpha_only);
    CHECK(request->output_mode == 1);
    CHECK(request->render_timeout_ms == 90000);
}

TEST_CASE("After Effects upscale popup maps both runtime resize methods",
          "[unit][adobe][effect][runtime][regression]") {
    std::array<PF_ParamDef, corridorkey::adobe::kEffectParameterSlotCount> storage{};
    std::array<PF_ParamDef*, corridorkey::adobe::kEffectParameterSlotCount> parameters{};
    for (std::size_t index = 0; index < storage.size(); ++index) {
        parameters[index] = &storage[index];
    }

    const corridorkey::adobe::AdobeEffectRuntimeRequestContext context{
        .models_root = "models",
        .host_surface = "after_effects",
        .effect_identity = corridorkey::adobe::kEffectMatchName,
        .client_instance_id = "sequence-1",
        .width = 3840,
        .height = 2160,
        .requested_device = corridorkey::DeviceInfo{"auto", 0, corridorkey::Backend::Auto},
        .engine_options = corridorkey::EngineCreateOptions{},
    };

    set_popup_value(storage[corridorkey::adobe::kParamUpscaleMethod], 1);
    auto lanczos_request =
        corridorkey::adobe::build_effect_runtime_request(parameters.data(), context);
    REQUIRE(lanczos_request.has_value());
    CHECK(lanczos_request->inference_params.upscale_method == corridorkey::UpscaleMethod::Lanczos4);

    set_popup_value(storage[corridorkey::adobe::kParamUpscaleMethod], 2);
    auto bilinear_request =
        corridorkey::adobe::build_effect_runtime_request(parameters.data(), context);
    REQUIRE(bilinear_request.has_value());
    CHECK(bilinear_request->inference_params.upscale_method ==
          corridorkey::UpscaleMethod::Bilinear);

    storage[corridorkey::adobe::kParamUpscaleMethod] = PF_ParamDef{};
    auto default_request =
        corridorkey::adobe::build_effect_runtime_request(parameters.data(), context);
    REQUIRE(default_request.has_value());
    CHECK(default_request->inference_params.upscale_method == corridorkey::UpscaleMethod::Bilinear);
}

TEST_CASE("After Effects render parameters map coarse-to-fine quality fallback",
          "[unit][adobe][effect][runtime][quality]") {
    std::array<PF_ParamDef, corridorkey::adobe::kEffectParameterSlotCount> storage{};
    std::array<PF_ParamDef*, corridorkey::adobe::kEffectParameterSlotCount> parameters{};
    for (std::size_t index = 0; index < storage.size(); ++index) {
        parameters[index] = &storage[index];
    }

    set_popup_value(storage[corridorkey::adobe::kParamQuality], 5);
    set_popup_value(storage[corridorkey::adobe::kParamQualityFallbackMode], 3);
    set_popup_value(storage[corridorkey::adobe::kParamCoarseResolutionOverride], 1);

    const corridorkey::adobe::AdobeEffectRuntimeRequestContext context{
        .models_root = "models",
        .host_surface = "after_effects",
        .effect_identity = corridorkey::adobe::kEffectMatchName,
        .client_instance_id = "sequence-1",
        .width = 3840,
        .height = 2160,
        .requested_device = corridorkey::DeviceInfo{"auto", 0, corridorkey::Backend::Auto},
        .engine_options = corridorkey::EngineCreateOptions{},
    };

    auto request = corridorkey::adobe::build_effect_runtime_request(parameters.data(), context);

    REQUIRE(request.has_value());
    CHECK(request->prepare_options.model_path ==
          std::filesystem::path{"models"} / "corridorkey_fp16_1024.onnx");
    CHECK(request->prepare_options.requested_resolution == 2048);
    CHECK(request->prepare_options.effective_resolution == 1024);
    CHECK(request->inference_params.target_resolution == 1024);
    CHECK(request->inference_params.requested_quality_resolution == 2048);
    CHECK(request->inference_params.quality_fallback_mode ==
          corridorkey::QualityFallbackMode::CoarseToFine);
    CHECK(request->inference_params.coarse_resolution_override == 1024);
}

TEST_CASE("After Effects source detail edge controls scale with source resolution",
          "[unit][adobe][effect][runtime][regression]") {
    std::array<PF_ParamDef, corridorkey::adobe::kEffectParameterSlotCount> storage{};
    std::array<PF_ParamDef*, corridorkey::adobe::kEffectParameterSlotCount> parameters{};
    for (std::size_t index = 0; index < storage.size(); ++index) {
        parameters[index] = &storage[index];
    }

    set_slider_value(storage[corridorkey::adobe::kParamDetailsEdgeShrink], 3.0F);
    set_slider_value(storage[corridorkey::adobe::kParamDetailsEdgeFeather], 7.0F);

    const corridorkey::adobe::AdobeEffectRuntimeRequestContext context{
        .models_root = "models",
        .host_surface = "after_effects",
        .effect_identity = corridorkey::adobe::kEffectMatchName,
        .client_instance_id = "sequence-1",
        .width = 3840,
        .height = 2160,
        .requested_device = corridorkey::DeviceInfo{"auto", 0, corridorkey::Backend::Auto},
        .engine_options = corridorkey::EngineCreateOptions{},
    };

    auto request = corridorkey::adobe::build_effect_runtime_request(parameters.data(), context);

    REQUIRE(request.has_value());
    CHECK(request->inference_params.sp_erode_px == 6);
    CHECK(request->inference_params.sp_blur_px == 14);
}

TEST_CASE("After Effects linear input color space maps runtime request to linear input",
          "[unit][adobe][effect][runtime][color]") {
    std::array<PF_ParamDef, corridorkey::adobe::kEffectParameterSlotCount> storage{};
    std::array<PF_ParamDef*, corridorkey::adobe::kEffectParameterSlotCount> parameters{};
    for (std::size_t index = 0; index < storage.size(); ++index) {
        parameters[index] = &storage[index];
    }

    set_popup_value(storage[corridorkey::adobe::kParamQuality], 3);
    set_popup_value(storage[corridorkey::adobe::kParamInputColorSpace], 2);

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
    CHECK(request->inference_params.input_is_linear);
}

TEST_CASE("After Effects Blue effect identity drives runtime model and despill channel",
          "[unit][adobe][effect][runtime][screen-color][regression]") {
    std::array<PF_ParamDef, corridorkey::adobe::kEffectParameterSlotCount> storage{};
    std::array<PF_ParamDef*, corridorkey::adobe::kEffectParameterSlotCount> parameters{};
    for (std::size_t index = 0; index < storage.size(); ++index) {
        parameters[index] = &storage[index];
    }

    set_popup_value(storage[corridorkey::adobe::kParamQuality], 3);

    const corridorkey::adobe::AdobeEffectRuntimeRequestContext context{
        .models_root = "models",
        .host_surface = "after_effects",
        .effect_identity = "com.corridorkey.effect.blue",
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
    CHECK(request->prepare_options.requested_resolution == 1024);
    CHECK(request->prepare_options.effective_resolution == 1024);
    CHECK(request->inference_params.target_resolution == 1024);
    CHECK(request->inference_params.despill_screen_channel == 2);
    CHECK_FALSE(request->inference_params.source_passthrough);
}

TEST_CASE("After Effects Green effect maps legacy blue screen choice to channel swap",
          "[unit][adobe][effect][runtime][screen-color][regression]") {
    std::array<PF_ParamDef, corridorkey::adobe::kEffectParameterSlotCount> storage{};
    std::array<PF_ParamDef*, corridorkey::adobe::kEffectParameterSlotCount> parameters{};
    for (std::size_t index = 0; index < storage.size(); ++index) {
        parameters[index] = &storage[index];
    }

    set_popup_value(storage[corridorkey::adobe::kParamScreenColor], 2);
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
    CHECK(request->prepare_options.node_identity == "green");
    CHECK(request->prepare_options.model_path ==
          std::filesystem::path{"models"} / "corridorkey_fp16_1024.onnx");
    CHECK(request->prepare_options.requested_device.backend != corridorkey::Backend::TorchTRT);
    CHECK(request->screen_color_mode == corridorkey::ScreenColorMode::BlueGreen);
    CHECK(request->inference_params.despill_screen_channel == 1);
}

TEST_CASE("After Effects blue-green channel swap records green-domain canonicalization mode",
          "[unit][adobe][effect][runtime][screen-color][regression]") {
    std::array<PF_ParamDef, corridorkey::adobe::kEffectParameterSlotCount> storage{};
    std::array<PF_ParamDef*, corridorkey::adobe::kEffectParameterSlotCount> parameters{};
    for (std::size_t index = 0; index < storage.size(); ++index) {
        parameters[index] = &storage[index];
    }

    set_popup_value(storage[corridorkey::adobe::kParamScreenColor], 2);
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
    CHECK(request->prepare_options.node_identity == "green");
    CHECK(request->prepare_options.model_path ==
          std::filesystem::path{"models"} / "corridorkey_fp16_1024.onnx");
    CHECK(request->screen_color_mode == corridorkey::ScreenColorMode::BlueGreen);
    CHECK(request->inference_params.despill_screen_channel == 1);
    CHECK(request->inference_params.source_passthrough);
}

TEST_CASE("After Effects render parameters sanitize corrupted host slider values",
          "[unit][adobe][effect][runtime][regression]") {
    std::array<PF_ParamDef, corridorkey::adobe::kEffectParameterSlotCount> storage{};
    std::array<PF_ParamDef*, corridorkey::adobe::kEffectParameterSlotCount> parameters{};
    for (std::size_t index = 0; index < storage.size(); ++index) {
        parameters[index] = &storage[index];
    }

    set_popup_value(storage[corridorkey::adobe::kParamQuality], 3);
    set_slider_value(storage[corridorkey::adobe::kParamDespillStrength], 2.0F);
    set_slider_value(storage[corridorkey::adobe::kParamMatteClipBlack], -1.0F);
    set_slider_value(storage[corridorkey::adobe::kParamMatteClipWhite], 2.0F);
    set_slider_value(storage[corridorkey::adobe::kParamMatteShrinkGrow], -20.0F);
    set_slider_value(storage[corridorkey::adobe::kParamMatteEdgeBlur], 20.0F);
    set_slider_value(storage[corridorkey::adobe::kParamMatteGamma], 20.0F);
    set_slider_value(storage[corridorkey::adobe::kParamDetailsEdgeShrink], -20.0F);
    set_slider_value(storage[corridorkey::adobe::kParamDetailsEdgeFeather], 200.0F);
    set_slider_value(storage[corridorkey::adobe::kParamTileOverlap], 512.0F);
    set_slider_value(storage[corridorkey::adobe::kParamDespeckleSize], 5.0F);
    set_slider_value(storage[corridorkey::adobe::kParamPrepareTimeoutSeconds], -5.0F);
    set_slider_value(storage[corridorkey::adobe::kParamRenderTimeoutSeconds],
                     std::numeric_limits<PF_FpShort>::quiet_NaN());

    const corridorkey::adobe::AdobeEffectRuntimeRequestContext context{
        .models_root = "models",
        .host_surface = "after_effects",
        .effect_identity = "com.corridorkey.effect.blue",
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
    CHECK(request->prepare_options.requested_resolution == 1024);
    CHECK(request->prepare_options.effective_resolution == 1024);
    CHECK(request->inference_params.target_resolution == 1024);
    CHECK(request->prepare_options.prepare_timeout_ms == 10000);
    CHECK(request->matte_params.black_point == Catch::Approx(0.0));
    CHECK(request->matte_params.white_point == Catch::Approx(1.0));
    CHECK(request->matte_params.shrink_grow_pixels == Catch::Approx(-10.0));
    CHECK(request->matte_params.edge_blur_pixels == Catch::Approx(5.0));
    CHECK(request->matte_params.gamma == Catch::Approx(10.0));
    CHECK(request->inference_params.despill_strength == Catch::Approx(1.0F));
    CHECK(request->inference_params.sp_erode_px == 0);
    CHECK(request->inference_params.sp_blur_px == 100);
    CHECK(request->inference_params.tile_padding == 128);
    CHECK(request->inference_params.despeckle_size == 50);
    CHECK_FALSE(request->inference_params.source_passthrough);
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
