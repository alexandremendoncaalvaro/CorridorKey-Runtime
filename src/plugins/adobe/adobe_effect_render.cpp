#include "adobe_effect_render.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "AE_EffectCBSuites.h"
#include "AE_EffectPixelFormat.h"
#include "SP/SPBasic.h"
#include "adobe_bridge.hpp"
#include "adobe_effect_metadata.hpp"
#include "adobe_effect_parameters.hpp"
#include "app/host_plugin_runtime_client.hpp"
#include "common/local_ipc.hpp"
#include "common/runtime_paths.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

using AdobeStatus = PF_Err;

constexpr AdobeStatus kAdobeStatusOk = PF_Err_NONE;
constexpr AdobeStatus kAdobeStatusUnsupported = PF_Err_BAD_CALLBACK_PARAM;
constexpr std::size_t kMaximumWindowsModulePath = 32768;

enum class PixelFormatPolicy {
    AllowDepthFallback,
    RequireExactHostFormat,
};

void set_return_message(PF_OutData& output_data, const char* message) noexcept {
    std::snprintf(output_data.return_msg, sizeof(output_data.return_msg), "%s", message);
}

void set_error_message(PF_OutData& output_data, const char* message) noexcept {
    set_return_message(output_data, message);
    output_data.out_flags =
        static_cast<PF_OutFlags>(output_data.out_flags | PF_OutFlag_DISPLAY_ERROR_MESSAGE);
}

AdobeStatus reject_render(PF_OutData& output_data, const std::string& message) noexcept {
    set_error_message(output_data, message.c_str());
    return kAdobeStatusUnsupported;
}

void set_return_message_for_host_error(PF_OutData& output_data, AdobeStatus status,
                                       const char* message) noexcept {
    if (status != PF_Interrupt_CANCEL) {
        set_return_message(output_data, message);
    }
}

std::filesystem::path adobe_module_path() {
#if defined(_WIN32)
    HMODULE module = nullptr;
    const auto flags =
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(&adobe_module_path), &module) == 0) {
        return {};
    }

    std::vector<wchar_t> buffer(MAX_PATH);
    while (buffer.size() <= kMaximumWindowsModulePath) {
        const DWORD length =
            GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        const auto used = static_cast<std::size_t>(length);
        if (used < buffer.size()) {
            return std::filesystem::path(std::wstring(buffer.data(), used));
        }
        if (buffer.size() > (kMaximumWindowsModulePath / 2U)) {
            break;
        }
        buffer.resize(buffer.size() * 2U);
    }
    return {};
#else
    return {};
#endif
}

class WorldSuiteCheckout {
   public:
    explicit WorldSuiteCheckout(PF_InData* input_data) noexcept
        : m_basic(input_data == nullptr ? nullptr : input_data->pica_basicP) {
        if (m_basic == nullptr || m_basic->AcquireSuite == nullptr) {
            return;
        }

        const void* suite = nullptr;
        if (m_basic->AcquireSuite(kPFWorldSuite, kPFWorldSuiteVersion2, &suite) == kSPNoError &&
            suite != nullptr) {
            m_suite = static_cast<const PF_WorldSuite2*>(suite);
        }
    }

    WorldSuiteCheckout(const WorldSuiteCheckout&) = delete;
    WorldSuiteCheckout& operator=(const WorldSuiteCheckout&) = delete;

    ~WorldSuiteCheckout() {
        if (m_suite != nullptr && m_basic != nullptr && m_basic->ReleaseSuite != nullptr) {
            m_basic->ReleaseSuite(kPFWorldSuite, kPFWorldSuiteVersion2);
        }
    }

    const PF_WorldSuite2* get() const noexcept {
        return m_suite;
    }

   private:
    SPBasicSuite* m_basic = nullptr;
    const PF_WorldSuite2* m_suite = nullptr;
};

corridorkey::Result<corridorkey::adobe::AdobePixelFormat> map_adobe_pixel_format(
    PF_PixelFormat pixel_format) {
    switch (pixel_format) {
        case PF_PixelFormat_ARGB128:
            return corridorkey::adobe::AdobePixelFormat::Argb128;
        case PF_PixelFormat_ARGB64:
            return corridorkey::adobe::AdobePixelFormat::Argb64;
        case PF_PixelFormat_ARGB32:
            return corridorkey::adobe::AdobePixelFormat::Argb32;
        case PF_PixelFormat_BGRA32:
            return corridorkey::adobe::AdobePixelFormat::Bgra32;
        default:
            return corridorkey::Unexpected<corridorkey::Error>(corridorkey::Error{
                corridorkey::ErrorCode::InvalidParameters, "Unsupported Adobe pixel format."});
    }
}

corridorkey::Result<corridorkey::adobe::AdobePixelFormat> pixel_format_for_world(
    PF_InData* input_data, const PF_LayerDef& world, PixelFormatPolicy policy) {
    WorldSuiteCheckout world_suite{input_data};
    const PF_WorldSuite2* suite = world_suite.get();
    if (suite != nullptr && suite->PF_GetPixelFormat != nullptr) {
        PF_PixelFormat pixel_format = PF_PixelFormat_INVALID;
        const PF_Err status = suite->PF_GetPixelFormat(&world, &pixel_format);
        if (status == kAdobeStatusOk) {
            return map_adobe_pixel_format(pixel_format);
        }
    }

    if (policy == PixelFormatPolicy::RequireExactHostFormat) {
        return corridorkey::Unexpected<corridorkey::Error>(
            corridorkey::Error{corridorkey::ErrorCode::InvalidParameters,
                               "SmartFX render requires an exact Adobe pixel format."});
    }

    return PF_WORLD_IS_DEEP(&world) ? corridorkey::adobe::AdobePixelFormat::Argb64
                                    : corridorkey::adobe::AdobePixelFormat::Argb32;
}

corridorkey::Result<corridorkey::adobe::AdobeFrameView> frame_view_for_world(
    PF_InData* input_data, const PF_LayerDef& world, PixelFormatPolicy policy) {
    auto pixel_format = pixel_format_for_world(input_data, world, policy);
    if (!pixel_format) {
        return corridorkey::Unexpected<corridorkey::Error>(pixel_format.error());
    }
    return corridorkey::adobe::AdobeFrameView{
        .data = world.data,
        .data_size_bytes = 0,
        .width = static_cast<int>(world.width),
        .height = static_cast<int>(world.height),
        .row_bytes = static_cast<int>(world.rowbytes),
        .pixel_format = *pixel_format,
    };
}

corridorkey::Result<corridorkey::adobe::AdobeMutableFrameView> mutable_frame_view_for_world(
    PF_InData* input_data, PF_LayerDef& world, PixelFormatPolicy policy) {
    auto pixel_format = pixel_format_for_world(input_data, world, policy);
    if (!pixel_format) {
        return corridorkey::Unexpected<corridorkey::Error>(pixel_format.error());
    }
    return corridorkey::adobe::AdobeMutableFrameView{
        .data = world.data,
        .data_size_bytes = 0,
        .width = static_cast<int>(world.width),
        .height = static_cast<int>(world.height),
        .row_bytes = static_cast<int>(world.rowbytes),
        .pixel_format = *pixel_format,
    };
}

std::string client_instance_id_for(PF_InData* input_data) {
    if (input_data == nullptr || input_data->effect_ref == nullptr) {
        return "default";
    }
    const auto value = reinterpret_cast<std::uintptr_t>(input_data->effect_ref);
    return std::to_string(value);
}

std::uint64_t render_index_for(PF_InData* input_data) noexcept {
    if (input_data == nullptr || input_data->current_time < 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(input_data->current_time);
}

bool has_valid_pre_render_callbacks(const PF_PreRenderExtra* extra) noexcept {
    return extra != nullptr && extra->input != nullptr && extra->output != nullptr &&
           extra->cb != nullptr && extra->cb->checkout_layer != nullptr;
}

bool has_valid_smart_render_callbacks(const PF_SmartRenderExtra* extra) noexcept {
    return extra != nullptr && extra->input != nullptr && extra->cb != nullptr &&
           extra->cb->checkout_layer_pixels != nullptr &&
           extra->cb->checkin_layer_pixels != nullptr && extra->cb->checkout_output != nullptr;
}

class SmartLayerPixelsCheckout {
   public:
    SmartLayerPixelsCheckout(PF_SmartRenderCallbacks& callbacks, PF_ProgPtr effect_ref,
                             A_long checkout_id) noexcept
        : m_callbacks(&callbacks), m_effect_ref(effect_ref), m_checkout_id(checkout_id) {}

    SmartLayerPixelsCheckout(const SmartLayerPixelsCheckout&) = delete;
    SmartLayerPixelsCheckout& operator=(const SmartLayerPixelsCheckout&) = delete;

    ~SmartLayerPixelsCheckout() {
        if (m_checked_out) {
            m_callbacks->checkin_layer_pixels(m_effect_ref, m_checkout_id);
        }
    }

    void mark_checked_out() noexcept {
        m_checked_out = true;
    }

   private:
    PF_SmartRenderCallbacks* m_callbacks = nullptr;
    PF_ProgPtr m_effect_ref = nullptr;
    A_long m_checkout_id = 0;
    bool m_checked_out = false;
};

struct SmartPreRenderData {
    bool alpha_hint_layer_checked_out = false;
};

void delete_smart_pre_render_data(void* pre_render_data) noexcept {
    std::unique_ptr<SmartPreRenderData> owned{static_cast<SmartPreRenderData*>(pre_render_data)};
}

const SmartPreRenderData* smart_pre_render_data_for(const PF_SmartRenderExtra* extra) noexcept {
    if (extra == nullptr || extra->input == nullptr || extra->input->pre_render_data == nullptr) {
        return nullptr;
    }
    return static_cast<const SmartPreRenderData*>(extra->input->pre_render_data);
}

class CheckedOutLayerParameter {
   public:
    CheckedOutLayerParameter(PF_InData* input_data, PF_ParamIndex index) noexcept
        : m_input_data(input_data) {
        if (m_input_data == nullptr || m_input_data->inter.checkout_param == nullptr ||
            m_input_data->inter.checkin_param == nullptr) {
            return;
        }

        m_status = m_input_data->inter.checkout_param(
            m_input_data->effect_ref, index, m_input_data->current_time, m_input_data->time_step,
            m_input_data->time_scale, &m_storage);
        m_checked_out = m_status == kAdobeStatusOk;
    }

    CheckedOutLayerParameter(const CheckedOutLayerParameter&) = delete;
    CheckedOutLayerParameter& operator=(const CheckedOutLayerParameter&) = delete;

    ~CheckedOutLayerParameter() {
        if (m_checked_out && m_input_data != nullptr &&
            m_input_data->inter.checkin_param != nullptr) {
            m_input_data->inter.checkin_param(m_input_data->effect_ref, &m_storage);
        }
    }

    [[nodiscard]] PF_Err status() const noexcept {
        return m_status;
    }

    [[nodiscard]] const PF_LayerDef* layer() const noexcept {
        if (!m_checked_out || m_storage.u.ld.data == nullptr) {
            return nullptr;
        }
        return &m_storage.u.ld;
    }

   private:
    PF_InData* m_input_data = nullptr;
    PF_ParamDef m_storage{};
    PF_Err m_status = kAdobeStatusOk;
    bool m_checked_out = false;
};

class CheckedOutEffectParameters {
   public:
    explicit CheckedOutEffectParameters(PF_InData& input_data) noexcept
        : m_input_data(&input_data) {}

    CheckedOutEffectParameters(const CheckedOutEffectParameters&) = delete;
    CheckedOutEffectParameters& operator=(const CheckedOutEffectParameters&) = delete;

    ~CheckedOutEffectParameters() {
        if (m_input_data == nullptr || m_input_data->inter.checkin_param == nullptr) {
            return;
        }
        for (std::size_t index = 0; index < m_checked_out.size(); ++index) {
            if (m_checked_out[index]) {
                m_input_data->inter.checkin_param(m_input_data->effect_ref, &m_storage[index]);
            }
        }
    }

    [[nodiscard]] bool can_checkout_non_layer_parameters() const noexcept {
        return m_input_data != nullptr && m_input_data->inter.checkout_param != nullptr &&
               m_input_data->inter.checkin_param != nullptr;
    }

    PF_Err checkout_non_layer_parameters() noexcept {
        for (std::size_t index = 1; index < m_storage.size(); ++index) {
            if (index == static_cast<std::size_t>(corridorkey::adobe::kParamAlphaHintLayer)) {
                continue;
            }

            const PF_Err status = m_input_data->inter.checkout_param(
                m_input_data->effect_ref, static_cast<PF_ParamIndex>(index),
                m_input_data->current_time, m_input_data->time_step, m_input_data->time_scale,
                &m_storage[index]);
            if (status != kAdobeStatusOk) {
                return status;
            }
            m_checked_out[index] = true;
            m_parameters[index] = &m_storage[index];
        }

        return kAdobeStatusOk;
    }

    void set_source_world(const PF_EffectWorld& source) noexcept {
        m_storage[corridorkey::adobe::kParamInputLayer].u.ld = source;
        m_parameters[corridorkey::adobe::kParamInputLayer] =
            &m_storage[corridorkey::adobe::kParamInputLayer];
    }

    PF_ParamDef** data() noexcept {
        return m_parameters.data();
    }

   private:
    PF_InData* m_input_data = nullptr;
    std::array<PF_ParamDef, corridorkey::adobe::kEffectParameterSlotCount> m_storage{};
    std::array<PF_ParamDef*, corridorkey::adobe::kEffectParameterSlotCount> m_parameters{};
    std::array<bool, corridorkey::adobe::kEffectParameterSlotCount> m_checked_out{};
};

corridorkey::adobe::AdobeEffectRuntimeRequestContext render_context_for(PF_InData* input_data,
                                                                        const PF_LayerDef& source) {
    const auto module_path = adobe_module_path();
    return corridorkey::adobe::AdobeEffectRuntimeRequestContext{
        .models_root = corridorkey::adobe::resolve_adobe_models_root(module_path),
        .host_surface = "after_effects",
        .effect_identity = corridorkey::adobe::kEffectMatchName,
        .client_instance_id = client_instance_id_for(input_data),
        .width = static_cast<int>(source.width),
        .height = static_cast<int>(source.height),
        .requested_device = corridorkey::DeviceInfo{"auto", 0, corridorkey::Backend::Auto},
        .engine_options = corridorkey::EngineCreateOptions{.allow_cpu_fallback = false,
                                                           .disable_cpu_ep_fallback = true},
    };
}

corridorkey::app::HostPluginRuntimeClientOptions client_options_for(
    const corridorkey::adobe::AdobeEffectRuntimeRequest& request) {
    corridorkey::app::HostPluginRuntimeClientOptions options;
    options.endpoint = corridorkey::common::default_host_plugin_runtime_endpoint();
    options.endpoint.port = corridorkey::common::default_host_plugin_runtime_port_for_family(
        request.prepare_options.effect_identity);
    options.server_binary =
        corridorkey::app::resolve_host_plugin_runtime_server_binary(adobe_module_path());
    options.request_timeout_ms = request.render_timeout_ms;
    options.prepare_timeout_ms = request.prepare_options.prepare_timeout_ms;
    return options;
}

PF_Err render_frame_with_pixel_format_policy(PF_InData* input_data, PF_OutData& output_data,
                                             PF_ParamDef* parameters[], PF_LayerDef* output,
                                             const PF_LayerDef* external_alpha_hint_layer,
                                             PixelFormatPolicy pixel_format_policy) {
    if (parameters == nullptr || parameters[corridorkey::adobe::kParamInputLayer] == nullptr ||
        output == nullptr) {
        return reject_render(output_data, "CorridorKey render requires source and output frames.");
    }

    const PF_LayerDef& source = parameters[corridorkey::adobe::kParamInputLayer]->u.ld;
    auto request = corridorkey::adobe::build_effect_runtime_request(
        parameters, render_context_for(input_data, source));
    if (!request) {
        return reject_render(output_data, request.error().message);
    }

    const auto source_frame = frame_view_for_world(input_data, source, pixel_format_policy);
    if (!source_frame) {
        return reject_render(output_data, source_frame.error().message);
    }

    auto source_runtime_frame = corridorkey::adobe::copy_adobe_frame_to_runtime(*source_frame);
    if (!source_runtime_frame) {
        return reject_render(output_data, source_runtime_frame.error().message);
    }
    corridorkey::adobe::apply_adobe_input_color_space(*source_runtime_frame,
                                                      request->inference_params.input_is_linear);

    corridorkey::Result<corridorkey::adobe::AdobeFrameView> external_alpha_hint_frame =
        corridorkey::Unexpected<corridorkey::Error>(corridorkey::Error{
            corridorkey::ErrorCode::InvalidParameters, "Alpha Hint Layer is not connected."});
    const corridorkey::adobe::AdobeFrameView* external_alpha_hint_view = nullptr;
    if (external_alpha_hint_layer != nullptr && external_alpha_hint_layer->data != nullptr) {
        external_alpha_hint_frame =
            frame_view_for_world(input_data, *external_alpha_hint_layer, pixel_format_policy);
        if (!external_alpha_hint_frame) {
            return reject_render(output_data, external_alpha_hint_frame.error().message);
        }
        external_alpha_hint_view = &*external_alpha_hint_frame;
    }

    corridorkey::ScreenColorTransform screen_color_transform;
    corridorkey::adobe::AdobeRuntimeFrame* runtime_input_frame = &*source_runtime_frame;
    corridorkey::adobe::AdobeRuntimeFrame screen_color_runtime_frame;
    if (corridorkey::screen_color_requires_green_domain_canonicalization(
            request->screen_color_mode)) {
        auto transformed_runtime_frame =
            corridorkey::adobe::copy_adobe_frame_to_runtime(*source_frame);
        if (!transformed_runtime_frame) {
            return reject_render(output_data, transformed_runtime_frame.error().message);
        }
        corridorkey::adobe::apply_adobe_input_color_space(
            *transformed_runtime_frame, request->inference_params.input_is_linear);

        screen_color_transform =
            corridorkey::adobe::canonicalize_adobe_runtime_frame_for_screen_color(
                *transformed_runtime_frame, request->screen_color_mode);
        screen_color_runtime_frame = std::move(*transformed_runtime_frame);
        runtime_input_frame = &screen_color_runtime_frame;
    }

    auto alpha_hint_source = corridorkey::adobe::resolve_alpha_hint_source(
        *runtime_input_frame, external_alpha_hint_view,
        request->inference_params.alpha_hint_policy);
    if (!alpha_hint_source) {
        return reject_render(output_data, alpha_hint_source.error().message);
    }

    const auto output_frame =
        mutable_frame_view_for_world(input_data, *output, pixel_format_policy);
    if (!output_frame) {
        return reject_render(output_data, output_frame.error().message);
    }

    auto runtime_client =
        corridorkey::app::HostPluginRuntimeClient::create(client_options_for(*request));
    if (!runtime_client) {
        return reject_render(output_data, runtime_client.error().message);
    }

    corridorkey::adobe::AdobeRuntimeBridge bridge(std::move(*runtime_client));
    auto prepare_status = bridge.prepare_session(request->prepare_options);
    if (!prepare_status) {
        return reject_render(output_data, prepare_status.error().message);
    }

    auto render_result = bridge.process_frame(*runtime_input_frame, request->inference_params,
                                              render_index_for(input_data));
    if (!render_result) {
        return reject_render(output_data, render_result.error().message);
    }
    corridorkey::AlphaEdgeState alpha_edge_state;
    corridorkey::adobe::apply_adobe_matte_params(*render_result, request->matte_params,
                                                 source.width, source.height, alpha_edge_state);
    if (!request->inference_params.output_alpha_only) {
        corridorkey::restore_from_green_domain(render_result->foreground.view(),
                                               screen_color_transform);
    }

    auto write_status = corridorkey::adobe::copy_runtime_result_to_adobe_frame(
        *render_result, *output_frame, request->output_mode, &*source_runtime_frame);
    if (!write_status) {
        return reject_render(output_data, write_status.error().message);
    }

    return kAdobeStatusOk;
}

}  // namespace

namespace corridorkey::adobe {

std::filesystem::path resolve_adobe_models_root(const std::filesystem::path& plugin_module_path) {
    if (auto override_path = common::environment_variable_copy("CORRIDORKEY_MODELS_DIR");
        override_path.has_value()) {
        return std::filesystem::path(*override_path);
    }

    const auto plugin_dir = plugin_module_path.parent_path();
    if (!plugin_module_path.empty() && !plugin_dir.empty()) {
        const auto contents_dir = plugin_dir.parent_path();
        if (plugin_dir.filename() == "Win64" && contents_dir.filename() == "Contents") {
            return contents_dir / "Resources" / "models";
        }

        return plugin_dir / "models";
    }

    return common::default_models_root();
}

PF_Err smart_pre_render(PF_InData* input_data, PF_OutData& output_data, void* extra) {
    const auto* const pre_render_input = static_cast<const PF_PreRenderExtra*>(extra);
    if (input_data == nullptr || !has_valid_pre_render_callbacks(pre_render_input)) {
        return reject_render(output_data,
                             "CorridorKey SmartFX pre-render requires host callbacks.");
    }

    auto* pre_render = static_cast<PF_PreRenderExtra*>(extra);
    PF_RenderRequest request = pre_render->input->output_request;
    PF_CheckoutResult source_result{};
    const PF_Err checkout_status = pre_render->cb->checkout_layer(
        input_data->effect_ref, kParamInputLayer, kParamInputLayer, &request,
        input_data->current_time, input_data->time_step, input_data->time_scale, &source_result);
    if (checkout_status != kAdobeStatusOk) {
        set_return_message_for_host_error(
            output_data, checkout_status,
            "CorridorKey SmartFX pre-render could not checkout source.");
        return checkout_status;
    }

    auto pre_render_data = std::make_unique<SmartPreRenderData>();
    PF_CheckoutResult alpha_hint_result{};
    const PF_Err alpha_hint_checkout_status = pre_render->cb->checkout_layer(
        input_data->effect_ref, kParamAlphaHintLayer, kParamAlphaHintLayer, &request,
        input_data->current_time, input_data->time_step, input_data->time_scale,
        &alpha_hint_result);
    pre_render_data->alpha_hint_layer_checked_out = alpha_hint_checkout_status == kAdobeStatusOk;

    pre_render->output->result_rect = source_result.result_rect;
    pre_render->output->max_result_rect = source_result.max_result_rect;
    pre_render->output->solid = FALSE;
    pre_render->output->flags = 0;
    pre_render->output->pre_render_data = pre_render_data.release();
    pre_render->output->delete_pre_render_data_func = delete_smart_pre_render_data;
    return kAdobeStatusOk;
}

PF_Err smart_render(PF_InData* input_data, PF_OutData& output_data, PF_ParamDef*[], void* extra) {
    const auto* const smart_render_input = static_cast<const PF_SmartRenderExtra*>(extra);
    if (input_data == nullptr || !has_valid_smart_render_callbacks(smart_render_input)) {
        return reject_render(output_data, "CorridorKey SmartFX render requires host callbacks.");
    }

    CheckedOutEffectParameters smart_parameters{*input_data};
    if (!smart_parameters.can_checkout_non_layer_parameters()) {
        return reject_render(output_data,
                             "CorridorKey SmartFX render requires parameter checkout callbacks.");
    }

    PF_Err status = smart_parameters.checkout_non_layer_parameters();
    if (status != kAdobeStatusOk) {
        return status;
    }

    auto* smart_render_extra = static_cast<PF_SmartRenderExtra*>(extra);
    PF_EffectWorld* input_world = nullptr;
    status = smart_render_extra->cb->checkout_layer_pixels(input_data->effect_ref, kParamInputLayer,
                                                           &input_world);
    if (status != kAdobeStatusOk) {
        set_return_message_for_host_error(output_data, status,
                                          "CorridorKey SmartFX render could not checkout source.");
        return status;
    }

    SmartLayerPixelsCheckout input_checkout{*smart_render_extra->cb, input_data->effect_ref,
                                            kParamInputLayer};
    input_checkout.mark_checked_out();
    if (input_world == nullptr) {
        return reject_render(output_data, "CorridorKey SmartFX render source is null.");
    }

    PF_EffectWorld* alpha_hint_world = nullptr;
    std::unique_ptr<SmartLayerPixelsCheckout> alpha_hint_checkout;
    const SmartPreRenderData* pre_render_data = smart_pre_render_data_for(smart_render_input);
    if (pre_render_data != nullptr && pre_render_data->alpha_hint_layer_checked_out) {
        const PF_Err alpha_hint_status = smart_render_extra->cb->checkout_layer_pixels(
            input_data->effect_ref, kParamAlphaHintLayer, &alpha_hint_world);
        if (alpha_hint_status == kAdobeStatusOk) {
            alpha_hint_checkout = std::make_unique<SmartLayerPixelsCheckout>(
                *smart_render_extra->cb, input_data->effect_ref, kParamAlphaHintLayer);
            alpha_hint_checkout->mark_checked_out();
        } else {
            alpha_hint_world = nullptr;
        }
    }

    PF_EffectWorld* output_world = nullptr;
    status = smart_render_extra->cb->checkout_output(input_data->effect_ref, &output_world);
    if (status != kAdobeStatusOk) {
        set_return_message_for_host_error(output_data, status,
                                          "CorridorKey SmartFX render could not checkout output.");
        return status;
    }
    if (output_world == nullptr) {
        return reject_render(output_data, "CorridorKey SmartFX render output is null.");
    }

    smart_parameters.set_source_world(*input_world);
    return render_frame_with_pixel_format_policy(input_data, output_data, smart_parameters.data(),
                                                 output_world, alpha_hint_world,
                                                 PixelFormatPolicy::RequireExactHostFormat);
}

PF_Err render_frame(PF_InData* input_data, PF_OutData& output_data, PF_ParamDef* parameters[],
                    PF_LayerDef* output) {
    CheckedOutLayerParameter alpha_hint_layer{input_data, kParamAlphaHintLayer};
    const PF_LayerDef* alpha_hint_layer_view =
        alpha_hint_layer.status() == kAdobeStatusOk ? alpha_hint_layer.layer() : nullptr;
    return render_frame_with_pixel_format_policy(input_data, output_data, parameters, output,
                                                 alpha_hint_layer_view,
                                                 PixelFormatPolicy::AllowDepthFallback);
}

}  // namespace corridorkey::adobe
