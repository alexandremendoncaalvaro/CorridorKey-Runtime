#include "adobe_effect_render.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

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

void set_return_message(PF_OutData& output_data, const char* message) noexcept {
    std::snprintf(output_data.return_msg, sizeof(output_data.return_msg), "%s", message);
}

AdobeStatus reject_render(PF_OutData& output_data, const std::string& message) noexcept {
    set_return_message(output_data, message.c_str());
    return kAdobeStatusUnsupported;
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

corridorkey::adobe::AdobePixelFormat pixel_format_for_world(const PF_LayerDef& world) noexcept {
    return PF_WORLD_IS_DEEP(&world) ? corridorkey::adobe::AdobePixelFormat::Argb64
                                    : corridorkey::adobe::AdobePixelFormat::Argb32;
}

corridorkey::adobe::AdobeFrameView frame_view_for_world(const PF_LayerDef& world) noexcept {
    return corridorkey::adobe::AdobeFrameView{
        .data = world.data,
        .data_size_bytes = 0,
        .width = static_cast<int>(world.width),
        .height = static_cast<int>(world.height),
        .row_bytes = static_cast<int>(world.rowbytes),
        .pixel_format = pixel_format_for_world(world),
    };
}

corridorkey::adobe::AdobeMutableFrameView mutable_frame_view_for_world(
    PF_LayerDef& world) noexcept {
    return corridorkey::adobe::AdobeMutableFrameView{
        .data = world.data,
        .data_size_bytes = 0,
        .width = static_cast<int>(world.width),
        .height = static_cast<int>(world.height),
        .row_bytes = static_cast<int>(world.rowbytes),
        .pixel_format = pixel_format_for_world(world),
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

corridorkey::adobe::AdobeEffectRuntimeRequestContext render_context_for(PF_InData* input_data,
                                                                        const PF_LayerDef& source) {
    return corridorkey::adobe::AdobeEffectRuntimeRequestContext{
        .models_root = corridorkey::common::default_models_root(),
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

}  // namespace

namespace corridorkey::adobe {

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
        set_return_message(output_data,
                           "CorridorKey SmartFX pre-render could not checkout source.");
        return checkout_status;
    }

    pre_render->output->result_rect = source_result.result_rect;
    pre_render->output->max_result_rect = source_result.max_result_rect;
    pre_render->output->solid = FALSE;
    pre_render->output->flags = 0;
    pre_render->output->pre_render_data = nullptr;
    pre_render->output->delete_pre_render_data_func = nullptr;
    return kAdobeStatusOk;
}

PF_Err render_frame(PF_InData* input_data, PF_OutData& output_data, PF_ParamDef* parameters[],
                    PF_LayerDef* output) {
    if (parameters == nullptr || parameters[kParamInputLayer] == nullptr || output == nullptr) {
        return reject_render(output_data, "CorridorKey render requires source and output frames.");
    }

    const PF_LayerDef& source = parameters[kParamInputLayer]->u.ld;
    auto request = build_effect_runtime_request(parameters, render_context_for(input_data, source));
    if (!request) {
        return reject_render(output_data, request.error().message);
    }

    auto runtime_client = app::HostPluginRuntimeClient::create(client_options_for(*request));
    if (!runtime_client) {
        return reject_render(output_data, runtime_client.error().message);
    }

    AdobeRuntimeBridge bridge(std::move(*runtime_client));
    auto prepare_status = bridge.prepare_session(request->prepare_options);
    if (!prepare_status) {
        return reject_render(output_data, prepare_status.error().message);
    }

    const auto source_frame = frame_view_for_world(source);
    auto render_result =
        bridge.process_frame(source_frame, request->inference_params, render_index_for(input_data));
    if (!render_result) {
        return reject_render(output_data, render_result.error().message);
    }

    auto source_runtime_frame = copy_adobe_frame_to_runtime(source_frame);
    const AdobeRuntimeFrame* source_runtime =
        source_runtime_frame ? &*source_runtime_frame : nullptr;
    auto write_status =
        copy_runtime_result_to_adobe_frame(*render_result, mutable_frame_view_for_world(*output),
                                           request->output_mode, source_runtime);
    if (!write_status) {
        return reject_render(output_data, write_status.error().message);
    }

    return kAdobeStatusOk;
}

}  // namespace corridorkey::adobe
