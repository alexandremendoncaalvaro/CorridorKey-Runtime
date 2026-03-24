#include <algorithm>
#include <chrono>
#include <corridorkey/engine.hpp>
#include <filesystem>
#include <iomanip>
#include <new>
#include <sstream>
#include <string>

#include "app/runtime_contracts.hpp"
#include "common/runtime_paths.hpp"
#include "ofx_image_utils.hpp"
#include "ofx_logging.hpp"
#include "ofx_model_selection.hpp"
#include "ofx_runtime_client.hpp"
#include "ofx_shared.hpp"

#if defined(__APPLE__)
#include <dlfcn.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace corridorkey::ofx {

namespace {

std::string backend_label(Backend backend) {
    switch (backend) {
        case Backend::CPU:
            return "cpu";
        case Backend::CoreML:
            return "coreml";
        case Backend::CUDA:
            return "cuda";
        case Backend::TensorRT:
            return "tensorrt";
        case Backend::DirectML:
            return "dml";
        case Backend::WindowsML:
            return "winml";
        case Backend::OpenVINO:
            return "openvino";
        case Backend::MLX:
            return "mlx";
        default:
            return "auto";
    }
}

std::string processing_backend_label(Backend backend) {
    switch (backend) {
        case Backend::CPU:
            return "CPU";
        case Backend::CoreML:
            return "CoreML";
        case Backend::CUDA:
            return "CUDA GPU";
        case Backend::TensorRT:
            return "TensorRT GPU";
        case Backend::DirectML:
            return "DirectML GPU";
        case Backend::WindowsML:
            return "Windows AI";
        case Backend::OpenVINO:
            return "OpenVINO";
        case Backend::MLX:
            return "MLX GPU";
        default:
            return "Auto";
    }
}

std::string processing_device_label(const DeviceInfo& device) {
    if (!device.name.empty()) {
        return device.name;
    }
    return processing_backend_label(device.backend);
}

bool environment_flag_enabled(const char* name) {
    if (auto value = common::environment_variable_copy(name); value.has_value()) {
        return *value == "1" || *value == "true" || *value == "TRUE";
    }
    return false;
}

bool use_runtime_server_default(const std::filesystem::path& runtime_server_path) {
    if (environment_flag_enabled("CORRIDORKEY_OFX_FORCE_INPROCESS")) {
        return false;
    }
    return !runtime_server_path.empty() && std::filesystem::exists(runtime_server_path);
}

bool allow_inprocess_fallback() {
    return environment_flag_enabled("CORRIDORKEY_OFX_ALLOW_INPROCESS_FALLBACK");
}

bool backend_matches_request(const DeviceInfo& effective_device,
                             const DeviceInfo& requested_device);

EngineCreateOptions ofx_engine_options(const DeviceInfo& requested_device) {
    EngineCreateOptions options;
    if (requested_device.backend != Backend::CPU) {
        options.allow_cpu_fallback = false;
        options.disable_cpu_ep_fallback = false;
    }
    return options;
}

std::string requested_quality_label(int quality_mode, int requested_resolution,
                                    bool cpu_quality_guardrail_active) {
    std::string label;
    if (quality_mode == kQualityAuto && requested_resolution > 0) {
        label = std::string(quality_mode_label(quality_mode)) + " (" +
                std::to_string(requested_resolution) + "px target)";
    } else {
        label = quality_mode_label(quality_mode);
    }
    if (cpu_quality_guardrail_active) {
        label += " [CPU capped to 512]";
    }
    return label;
}

std::string effective_quality_label(int resolution) {
    if (resolution <= 0) {
        return "Not loaded";
    }
    return std::to_string(resolution) + "px";
}

std::string runtime_artifact_label(const std::filesystem::path& model_path) {
    if (model_path.empty()) {
        return "Not loaded";
    }
    return model_path.filename().string();
}

std::string truncate_status_message(const std::string& message, std::size_t max_length) {
    if (message.size() <= max_length) {
        return message;
    }
    if (max_length <= 3) {
        return message.substr(0, max_length);
    }
    return message.substr(0, max_length - 3) + "...";
}

std::string format_duration_ms(double ms) {
    if (ms <= 0.0) {
        return "n/a";
    }
    std::ostringstream oss;
    if (ms >= 1000.0) {
        oss << std::fixed << std::setprecision(1) << (ms / 1000.0) << " s";
    } else {
        oss << std::fixed << std::setprecision(1) << ms << " ms";
    }
    return oss.str();
}

std::string runtime_status_label(const InstanceData& data) {
    if (!data.last_error.empty()) {
        return "Error: " + truncate_status_message(data.last_error, 160);
    }
    std::string status;
    if (!data.last_warning.empty()) {
        status = "Note: " + truncate_status_message(data.last_warning, 100);
    }
    if (data.last_frame_ms > 0.0) {
        if (!status.empty()) {
            status += " | ";
        }
        status += "Last frame: " + format_duration_ms(data.last_frame_ms) +
                  " | Avg: " + format_duration_ms(data.avg_frame_ms);
    }
    return status.empty() ? "Idle" : status;
}

double elapsed_ms_since(std::chrono::steady_clock::time_point start_time) {
    const auto elapsed = std::chrono::steady_clock::now() - start_time;
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

void log_stage_timing(std::string_view scope, std::string_view phase,
                      const DeviceInfo& requested_device,
                      const std::filesystem::path& artifact_path, int requested_resolution,
                      int effective_resolution, const StageTiming& timing) {
    log_message(scope, "event=stage phase=" + std::string(phase) + " stage=" + timing.name +
                           " total_ms=" + std::to_string(timing.total_ms) +
                           " requested_backend=" + backend_label(requested_device.backend) +
                           " artifact=" + artifact_path.filename().string() +
                           " requested_resolution=" + std::to_string(requested_resolution) +
                           " effective_resolution=" + std::to_string(effective_resolution));
}

void log_engine_event(std::string_view scope, std::string_view event, std::string_view phase,
                      const DeviceInfo& requested_device, const DeviceInfo& effective_device,
                      const std::filesystem::path& artifact_path, int requested_resolution,
                      int effective_resolution,
                      const std::optional<BackendFallbackInfo>& fallback = std::nullopt,
                      std::string_view detail = {}) {
    std::string message = "event=" + std::string(event) + " phase=" + std::string(phase) +
                          " requested_backend=" + backend_label(requested_device.backend) +
                          " effective_backend=" + backend_label(effective_device.backend) +
                          " requested_device=" + requested_device.name +
                          " effective_device=" + effective_device.name +
                          " artifact=" + artifact_path.filename().string() +
                          " requested_resolution=" + std::to_string(requested_resolution) +
                          " effective_resolution=" + std::to_string(effective_resolution);
    if (fallback.has_value() && !fallback->reason.empty()) {
        message += " fallback_reason=" + fallback->reason;
    }
    if (!detail.empty()) {
        message += " detail=" + std::string(detail);
    }
    log_message(scope, message);
}

void set_string_param_value(OfxParamHandle param, const std::string& value) {
    if (g_suites.parameter == nullptr || param == nullptr) {
        return;
    }
    g_suites.parameter->paramSetValue(param, value.c_str());
}

void update_runtime_panel_values(InstanceData* data) {
    if (data == nullptr) {
        return;
    }

    bool has_session = false;
    if (data->use_runtime_server) {
        has_session = data->runtime_client != nullptr && data->runtime_client->has_session();
    } else {
        has_session = data->engine != nullptr;
    }
    const bool is_loading = !has_session && data->last_error.empty();

    set_string_param_value(data->runtime_processing_param,
                           processing_backend_label(data->device.backend));
    set_string_param_value(data->runtime_device_param, processing_device_label(data->device));
    set_string_param_value(
        data->runtime_requested_quality_param,
        requested_quality_label(data->active_quality_mode, data->requested_resolution,
                                data->cpu_quality_guardrail_active));
    set_string_param_value(
        data->runtime_effective_quality_param,
        is_loading ? "Loading..." : effective_quality_label(data->active_resolution));
    set_string_param_value(data->runtime_artifact_param,
                           is_loading ? "Loading..." : runtime_artifact_label(data->model_path));
    set_string_param_value(data->runtime_status_param,
                           is_loading ? "Loading..." : runtime_status_label(*data));
}

void set_runtime_panel_status(InstanceData* data, const std::string& processing,
                              const std::string& device, const std::string& requested_quality,
                              const std::string& effective_quality, const std::string& artifact,
                              const std::string& status) {
    if (data == nullptr) {
        return;
    }

    set_string_param_value(data->runtime_processing_param, processing);
    set_string_param_value(data->runtime_device_param, device);
    set_string_param_value(data->runtime_requested_quality_param, requested_quality);
    set_string_param_value(data->runtime_effective_quality_param, effective_quality);
    set_string_param_value(data->runtime_artifact_param, artifact);
    set_string_param_value(data->runtime_status_param, status);
}

bool backend_matches_request(const Engine& engine, const DeviceInfo& requested_device) {
    return backend_matches_request(engine.current_device(), requested_device);
}

bool backend_matches_request(const DeviceInfo& effective_device,
                             const DeviceInfo& requested_device) {
    if (requested_device.backend == Backend::CPU) {
        return true;
    }
    return effective_device.backend == requested_device.backend;
}

std::string backend_mismatch_message(const std::string& operation,
                                     const DeviceInfo& requested_device, const Engine& engine,
                                     const std::filesystem::path& artifact_path) {
    return operation + " requested backend " + backend_label(requested_device.backend) + " for " +
           artifact_path.filename().string() + " but the engine is using " +
           backend_label(engine.current_device().backend) + ".";
}

std::string backend_mismatch_message(const std::string& operation,
                                     const DeviceInfo& requested_device, const Engine& engine,
                                     const std::filesystem::path& artifact_path,
                                     const std::optional<BackendFallbackInfo>& fallback) {
    std::string message =
        backend_mismatch_message(operation, requested_device, engine, artifact_path);
    if (fallback.has_value() && !fallback->reason.empty()) {
        message += " Reason: " + fallback->reason;
    }
    return message;
}

std::optional<std::filesystem::path> plugin_module_path() {
#if defined(_WIN32)
    HMODULE module = nullptr;
    auto address = reinterpret_cast<LPCWSTR>(&plugin_module_path);
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            address, &module)) {
        log_message("plugin_module_path", "GetModuleHandleExW failed.");
        return std::nullopt;
    }

    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
        log_message("plugin_module_path", "GetModuleFileNameW returned empty path.");
        return std::nullopt;
    }
    buffer.resize(length);
    return std::filesystem::path(buffer);
#elif defined(__APPLE__)
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(&plugin_module_path), &info) == 0 ||
        info.dli_fname == nullptr) {
        log_message("plugin_module_path", "dladdr failed to resolve module path.");
        return std::nullopt;
    }
    return std::filesystem::path(info.dli_fname);
#else
    return std::nullopt;
#endif
}

std::filesystem::path resolve_models_root() {
    if (auto override_path = common::environment_variable_copy("CORRIDORKEY_MODELS_DIR");
        override_path.has_value()) {
        log_message("resolve_models_root", std::string("Using override: ") + *override_path);
        return std::filesystem::path(*override_path);
    }

    if (auto module_path = plugin_module_path(); module_path.has_value()) {
        auto resources = module_path->parent_path().parent_path() / "Resources" / "models";
        std::error_code error;
        if (std::filesystem::exists(resources, error) && !error) {
            log_message("resolve_models_root",
                        std::string("Using bundle resources: ") + resources.string());
            return resources;
        }
    }

    auto fallback = common::default_models_root();
    log_message("resolve_models_root", std::string("Using fallback: ") + fallback.string());
    return fallback;
}

app::OfxRuntimePrepareSessionRequest build_prepare_request(
    const BootstrapEngineCandidate& candidate) {
    app::OfxRuntimePrepareSessionRequest request;
    request.client_instance_id = "bootstrap";
    request.model_path = candidate.executable_model_path;
    request.artifact_name = candidate.requested_model_path.filename().string();
    request.requested_device = candidate.device;
    request.engine_options = ofx_engine_options(candidate.device);
    request.requested_quality_mode = kQualityAuto;
    request.requested_resolution = candidate.requested_resolution;
    request.effective_resolution = candidate.effective_resolution;
    return request;
}

app::OfxRuntimePrepareSessionRequest build_prepare_request(
    const DeviceInfo& requested_device, const QualityArtifactSelection& selection,
    int requested_quality_mode) {
    app::OfxRuntimePrepareSessionRequest request;
    request.client_instance_id = "quality_switch";
    request.model_path = selection.executable_model_path;
    request.artifact_name = selection.executable_model_path.filename().string();
    request.requested_device = requested_device;
    request.engine_options = ofx_engine_options(requested_device);
    request.requested_quality_mode = requested_quality_mode;
    request.requested_resolution = selection.requested_resolution;
    request.effective_resolution = selection.effective_resolution;
    return request;
}

}  // namespace

InstanceData* get_instance_data(OfxImageEffectHandle instance) {
    if (g_suites.property == nullptr || g_suites.image_effect == nullptr) {
        return nullptr;
    }
    OfxPropertySetHandle props = nullptr;
    if (g_suites.image_effect->getPropertySet(instance, &props) != kOfxStatOK) {
        return nullptr;
    }
    void* ptr = nullptr;
    if (g_suites.property->propGetPointer(props, kOfxPropInstanceData, 0, &ptr) != kOfxStatOK) {
        return nullptr;
    }
    return reinterpret_cast<InstanceData*>(ptr);
}

void set_instance_data(OfxImageEffectHandle instance, InstanceData* data) {
    if (g_suites.property == nullptr || g_suites.image_effect == nullptr) {
        return;
    }
    OfxPropertySetHandle props = nullptr;
    if (g_suites.image_effect->getPropertySet(instance, &props) != kOfxStatOK) {
        return;
    }
    g_suites.property->propSetPointer(props, kOfxPropInstanceData, 0, data);
}

void set_param_enabled(OfxParamHandle param, bool enabled);
void sync_dependent_params(InstanceData* data);

OfxStatus create_instance(OfxImageEffectHandle instance) {
    const auto create_start = std::chrono::steady_clock::now();
    const auto log_create_total = [&](std::string_view outcome, std::string_view detail = {}) {
        std::string message = "event=instance_create_total total_ms=" +
                              std::to_string(elapsed_ms_since(create_start)) +
                              " outcome=" + std::string(outcome);
        if (!detail.empty()) {
            message += " detail=" + std::string(detail);
        }
        log_message("create_instance", message);
    };

    if (g_suites.image_effect == nullptr || g_suites.parameter == nullptr) {
        log_message("create_instance", "Missing required suites.");
        log_create_total("missing_suites");
        return kOfxStatErrMissingHostFeature;
    }

    auto data = std::unique_ptr<InstanceData>(new (std::nothrow) InstanceData());
    if (!data) {
        log_message("create_instance", "Failed to allocate InstanceData.");
        log_create_total("oom");
        return kOfxStatErrMemory;
    }
    data->effect = instance;
    data->cpu_quality_guardrail_active = false;
    data->render_count = 0;

    if (g_suites.image_effect->clipGetHandle(instance, "Source", &data->source_clip, nullptr) !=
        kOfxStatOK) {
        log_message("create_instance", "Failed to get Source clip handle.");
        log_create_total("source_clip_failed");
        return kOfxStatFailed;
    }

    g_suites.image_effect->clipGetHandle(instance, kClipAlphaHint, &data->alpha_hint_clip, nullptr);

    if (g_suites.image_effect->clipGetHandle(instance, "Output", &data->output_clip, nullptr) !=
        kOfxStatOK) {
        log_message("create_instance", "Failed to get Output clip handle.");
        log_create_total("output_clip_failed");
        return kOfxStatFailed;
    }

    OfxParamSetHandle param_set;
    if (g_suites.image_effect->getParamSet(instance, &param_set) != kOfxStatOK) {
        log_message("create_instance", "Failed to get param set.");
        log_create_total("param_set_failed");
        return kOfxStatFailed;
    }

    g_suites.parameter->paramGetHandle(param_set, kParamQualityMode, &data->quality_mode_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamOutputMode, &data->output_mode_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamInputColorSpace,
                                       &data->input_color_space_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamQuantizationMode,
                                       &data->quantization_mode_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamScreenColor, &data->screen_color_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamTemporalSmoothing,
                                       &data->temporal_smoothing_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamDespillStrength, &data->despill_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamSpillMethod, &data->spill_method_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamAutoDespeckle, &data->despeckle_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamDespeckleSize, &data->despeckle_size_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamAlphaBlackPoint,
                                       &data->alpha_black_point_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamAlphaWhitePoint,
                                       &data->alpha_white_point_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamAlphaErode, &data->alpha_erode_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamAlphaSoftness, &data->alpha_softness_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamAlphaGamma, &data->alpha_gamma_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamUpscaleMethod, &data->upscale_method_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamEnableTiling, &data->enable_tiling_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamTileOverlap, &data->tile_overlap_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamSourcePassthrough,
                                       &data->source_passthrough_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamEdgeErode, &data->edge_erode_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamEdgeBlur, &data->edge_blur_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRuntimeProcessing,
                                       &data->runtime_processing_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRuntimeDevice, &data->runtime_device_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRuntimeRequestedQuality,
                                       &data->runtime_requested_quality_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRuntimeEffectiveQuality,
                                       &data->runtime_effective_quality_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRuntimeArtifact,
                                       &data->runtime_artifact_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRuntimeStatus, &data->runtime_status_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRenderTimeout, &data->render_timeout_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamPrepareTimeout,
                                       &data->prepare_timeout_param, nullptr);

    sync_dependent_params(data.get());

    set_runtime_panel_status(data.get(), "Initializing...", "Detecting...", "Loading...",
                             "Loading...", "Loading...", "Loading...");

    int render_timeout_s = 30;
    int prepare_timeout_s = 120;
    if (data->render_timeout_param != nullptr) {
        g_suites.parameter->paramGetValue(data->render_timeout_param, &render_timeout_s);
    }
    if (data->prepare_timeout_param != nullptr) {
        g_suites.parameter->paramGetValue(data->prepare_timeout_param, &prepare_timeout_s);
    }

    data->runtime_server_path =
        resolve_ofx_runtime_server_binary(plugin_module_path().value_or(""));
    data->use_runtime_server = use_runtime_server_default(data->runtime_server_path);
    if (data->use_runtime_server) {
        OfxRuntimeClientOptions client_options;
        client_options.endpoint = common::default_ofx_runtime_endpoint();
        client_options.server_binary = data->runtime_server_path;
        client_options.request_timeout_ms = render_timeout_s * 1000;
        client_options.prepare_timeout_ms = prepare_timeout_s * 1000;
        auto runtime_client = OfxRuntimeClient::create(std::move(client_options));
        if (!runtime_client) {
            data->last_error = runtime_client.error().message;
            log_message("create_instance",
                        "Runtime client init failed: " + runtime_client.error().message);
            if (!allow_inprocess_fallback()) {
                post_message(kOfxMessageError, data->last_error.c_str(), instance);
                log_create_total("runtime_client_failed", data->last_error);
                return kOfxStatFailed;
            }
            data->use_runtime_server = false;
        } else {
            data->runtime_client = std::move(*runtime_client);
            log_message("create_instance", "Using out-of-process OFX runtime.");
        }
    } else {
        log_message("create_instance", "Using in-process OFX runtime.");
    }

    data->models_root = resolve_models_root();
    DeviceInfo detected_device = auto_detect();
    log_message("create_instance", std::string("Detected device: ") + detected_device.name);
    log_message("create_instance",
                std::string("Detected backend: ") + backend_label(detected_device.backend));
    auto capabilities = runtime_capabilities();
    log_message("create_instance", std::string("Platform: ") + capabilities.platform);

    int initial_quality_mode = kQualityAuto;
    if (data->quality_mode_param != nullptr) {
        g_suites.parameter->paramGetValue(data->quality_mode_param, &initial_quality_mode);
    }

    DeviceInfo preferred_device = detected_device;
#if defined(__APPLE__)
    if (capabilities.apple_silicon && capabilities.mlx_probe_available &&
        has_mlx_bootstrap_artifacts(data->models_root)) {
        preferred_device =
            DeviceInfo{"Apple Silicon MLX", detected_device.available_memory_mb, Backend::MLX};
    }
#endif
    data->preferred_device = preferred_device;
    data->device = detected_device;
    data->active_quality_mode = initial_quality_mode;
    data->requested_resolution =
        initial_requested_resolution_for_quality_mode(initial_quality_mode);
    data->active_resolution = 0;
    data->model_path.clear();
    data->last_error.clear();

    if (!should_prepare_bootstrap_during_instance_create(data->use_runtime_server)) {
        log_message("create_instance", "Deferring runtime session bootstrap until first render.");
        update_runtime_panel_values(data.get());
        set_instance_data(instance, data.release());
        log_create_total("success", "bootstrap=deferred");
        return kOfxStatOK;
    }

    auto bootstrap_candidates =
        build_bootstrap_candidates(capabilities, detected_device, data->models_root);
    if (bootstrap_candidates.empty()) {
        log_message("create_instance", "No compatible model artifacts found for OFX bootstrap.");
        post_message(kOfxMessageError, "No compatible model artifacts found for this device.",
                     instance);
        log_create_total("no_artifacts");
        return kOfxStatFailed;
    }

    std::string failure_summary;
    for (const auto& candidate : bootstrap_candidates) {
        constexpr std::string_view kBootstrapPhase = "bootstrap";
        log_message("create_instance",
                    std::string("Attempting backend: ") + backend_label(candidate.device.backend));
        log_message("create_instance", std::string("Requested model: ") +
                                           candidate.requested_model_path.filename().string());
        if (candidate.executable_model_path != candidate.requested_model_path) {
            log_message("create_instance", std::string("Requested executable artifact: ") +
                                               candidate.executable_model_path.filename().string());
        }

        log_engine_event("create_instance", "engine_create_begin", kBootstrapPhase,
                         candidate.device, candidate.device, candidate.executable_model_path,
                         candidate.requested_resolution, candidate.effective_resolution);

        DeviceInfo effective_device = candidate.device;
        std::optional<BackendFallbackInfo> fallback = std::nullopt;
        if (data->use_runtime_server && data->runtime_client != nullptr) {
            auto prepare_result = data->runtime_client->prepare_session(
                build_prepare_request(candidate), [&](const StageTiming& timing) {
                    log_stage_timing("create_instance", kBootstrapPhase, candidate.device,
                                     candidate.executable_model_path,
                                     candidate.requested_resolution, candidate.effective_resolution,
                                     timing);
                });
            if (!prepare_result) {
                std::string failure =
                    backend_label(candidate.device.backend) + ": " + prepare_result.error().message;
                log_engine_event("create_instance", "engine_create_error", kBootstrapPhase,
                                 candidate.device, candidate.device,
                                 candidate.executable_model_path, candidate.requested_resolution,
                                 candidate.effective_resolution, std::nullopt,
                                 prepare_result.error().message);
                log_message("create_instance", std::string("Runtime prepare failed: ") + failure);
                if (!failure_summary.empty()) {
                    failure_summary += " | ";
                }
                failure_summary += failure;
                continue;
            }
            effective_device = prepare_result->session.effective_device;
            fallback = prepare_result->session.backend_fallback;
            log_message("create_instance",
                        "Runtime session prepared. reused_existing_session=" +
                            std::to_string(prepare_result->session.reused_existing_session) +
                            " ref_count=" + std::to_string(prepare_result->session.ref_count));
        } else {
            auto engine_result = Engine::create(
                candidate.executable_model_path, candidate.device,
                [&](const StageTiming& timing) {
                    log_stage_timing("create_instance", kBootstrapPhase, candidate.device,
                                     candidate.executable_model_path,
                                     candidate.requested_resolution, candidate.effective_resolution,
                                     timing);
                },
                ofx_engine_options(candidate.device));
            if (!engine_result) {
                std::string failure =
                    backend_label(candidate.device.backend) + ": " + engine_result.error().message;
                log_engine_event("create_instance", "engine_create_error", kBootstrapPhase,
                                 candidate.device, candidate.device,
                                 candidate.executable_model_path, candidate.requested_resolution,
                                 candidate.effective_resolution, std::nullopt,
                                 engine_result.error().message);
                log_message("create_instance", std::string("Engine create failed: ") + failure);
                if (!failure_summary.empty()) {
                    failure_summary += " | ";
                }
                failure_summary += failure;
                continue;
            }

            auto engine = std::move(*engine_result);
            effective_device = engine->current_device();
            fallback = engine->backend_fallback();
            if (!backend_matches_request(*engine, candidate.device)) {
                std::string failure = backend_mismatch_message(
                    "Bootstrap", candidate.device, *engine, candidate.executable_model_path,
                    engine->backend_fallback());
                log_message("create_instance", failure);
                if (!failure_summary.empty()) {
                    failure_summary += " | ";
                }
                failure_summary += failure;
                continue;
            }
            data->engine = std::move(engine);
        }

        log_engine_event("create_instance", "engine_create_result", kBootstrapPhase,
                         candidate.device, effective_device, candidate.executable_model_path,
                         candidate.requested_resolution, candidate.effective_resolution, fallback);
        if (!backend_matches_request(effective_device, candidate.device)) {
            std::string failure =
                "Bootstrap requested backend " + backend_label(candidate.device.backend) + " for " +
                candidate.executable_model_path.filename().string() + " but the runtime is using " +
                backend_label(effective_device.backend) + ".";
            if (fallback.has_value() && !fallback->reason.empty()) {
                failure += " Reason: " + fallback->reason;
            }
            log_message("create_instance", failure);
            if (!failure_summary.empty()) {
                failure_summary += " | ";
            }
            failure_summary += failure;
            continue;
        }

        data->preferred_device = candidate.device;
        data->device = effective_device;
        data->model_path = candidate.executable_model_path;
        data->cached_result_valid = false;
        data->cached_signature = 0;
        data->cached_signature_valid = false;
        data->active_quality_mode = kQualityAuto;
        data->requested_resolution = candidate.requested_resolution;
        data->active_resolution = candidate.effective_resolution;
        data->cpu_quality_guardrail_active = false;
        data->render_count = 0;
        data->last_error.clear();
        data->last_frame_ms = 0.0;
        data->avg_frame_ms = 0.0;
        data->frame_time_samples = 0;

        if (candidate.device.backend != detected_device.backend) {
            log_message("create_instance", std::string("Backend fallback: ") +
                                               backend_label(detected_device.backend) + " -> " +
                                               backend_label(candidate.device.backend));
        }
        log_message("create_instance", std::string("Selected model: ") +
                                           candidate.requested_model_path.filename().string());
        if (candidate.executable_model_path != candidate.requested_model_path) {
            log_message("create_instance", std::string("Effective bridge: ") +
                                               candidate.executable_model_path.filename().string());
        }
        log_message("create_instance",
                    std::string("Effective backend: ") + backend_label(data->device.backend));
        log_message("create_instance",
                    "Effective resolution: " + std::to_string(data->active_resolution));
        log_message("create_instance", "Engine created successfully.");
        update_runtime_panel_values(data.get());

        set_instance_data(instance, data.release());
        log_create_total("success");
        return kOfxStatOK;
    }

    data->last_error = failure_summary;
    std::string failure_message = "Failed to load AI engine: " + failure_summary;
    post_message(kOfxMessageError, failure_message.c_str(), instance);
    log_create_total("engine_create_failed", failure_summary);
    return kOfxStatFailed;
}

bool ensure_engine_for_quality(InstanceData* data, int quality_mode, int input_width,
                               int input_height, int quantization_mode) {
    const auto quality_switch_start = std::chrono::steady_clock::now();
    const auto log_quality_total = [&](std::string_view outcome, std::string_view detail = {}) {
        std::string message = "event=quality_switch_total total_ms=" +
                              std::to_string(elapsed_ms_since(quality_switch_start)) +
                              " quality=" + quality_mode_label(quality_mode) +
                              " outcome=" + std::string(outcome);
        if (!detail.empty()) {
            message += " detail=" + std::string(detail);
        }
        log_message("ensure_engine_for_quality", message);
    };

    if (data == nullptr || (!data->use_runtime_server && data->engine == nullptr) ||
        (data->use_runtime_server && data->runtime_client == nullptr)) {
        log_quality_total("no_engine");
        return true;
    }

    DeviceInfo requested_device = data->preferred_device;
    if (requested_device.backend == Backend::Auto) {
        requested_device = data->device;
    }

    const int requested_quality_mode = quality_mode;
    const int effective_quality_mode =
        clamp_quality_mode_for_cpu_backend(requested_device.backend, requested_quality_mode);
    const bool cpu_quality_guardrail_active = effective_quality_mode != requested_quality_mode;
    const int requested_resolution =
        resolve_target_resolution(requested_quality_mode, input_width, input_height);
    if (requested_device.backend == Backend::TensorRT && quantization_mode == kQuantizationInt8) {
        data->last_error =
            "INT8 (Compact) is not supported by the TensorRT RTX execution provider. "
            "Please use FP16 (Full).";
        log_message("ensure_engine_for_quality", data->last_error);
        update_runtime_panel(data);
        log_quality_total("unsupported_quantization", data->last_error);
        return false;
    }
    auto selections = quality_artifact_candidates(data->models_root, requested_device.backend,
                                                  effective_quality_mode, input_width, input_height,
                                                  quantization_mode);
    data->cpu_quality_guardrail_active = cpu_quality_guardrail_active;
    if (cpu_quality_guardrail_active) {
        data->last_error = "CPU backend is limited to Draft (512) for interactive rendering. " +
                           std::string(quality_mode_label(requested_quality_mode)) +
                           " will run as Draft (512).";
        log_message("ensure_engine_for_quality", data->last_error);
    }
    if (selections.empty()) {
        if (cpu_quality_guardrail_active) {
            data->last_error =
                "CPU backend is limited to Draft (512), but the 512 artifact is missing for " +
                backend_label(requested_device.backend) + ".";
        } else {
            data->last_error =
                "Requested quality " + std::string(quality_mode_label(requested_quality_mode)) +
                " requires a " + std::to_string(requested_resolution) + "px artifact for backend " +
                backend_label(requested_device.backend) + ", but that artifact is missing.";
        }
        log_message("ensure_engine_for_quality", data->last_error);
        update_runtime_panel(data);
        log_quality_total("missing_artifact", data->last_error);
        return false;
    }

    if (!data->use_runtime_server && data->engine != nullptr) {
        data->device = data->engine->current_device();
    } else if (data->use_runtime_server && data->runtime_client != nullptr &&
               data->runtime_client->has_session()) {
        data->device = data->runtime_client->current_device();
    }
    bool current_backend_matches = backend_matches_request(data->device, requested_device);

    for (const auto& selection : selections) {
        const bool session_alive = data->use_runtime_server ? (data->runtime_client != nullptr &&
                                                               data->runtime_client->has_session())
                                                            : (data->engine != nullptr);
        if (current_backend_matches && session_alive &&
            selection.executable_model_path == data->model_path &&
            selection.effective_resolution == data->active_resolution) {
            data->active_quality_mode = requested_quality_mode;
            data->requested_resolution = requested_resolution;
            data->active_resolution = selection.effective_resolution;
            data->last_error.clear();
            update_runtime_panel_values(data);
            log_quality_total("reused_engine");
            return true;
        }

        constexpr std::string_view kQualitySwitchPhase = "quality_switch";
        log_engine_event("ensure_engine_for_quality", "engine_create_begin", kQualitySwitchPhase,
                         requested_device, requested_device, selection.executable_model_path,
                         requested_resolution, selection.effective_resolution);

        DeviceInfo effective_device = requested_device;
        std::optional<BackendFallbackInfo> fallback = std::nullopt;
        if (data->use_runtime_server && data->runtime_client != nullptr) {
            set_string_param_value(
                data->runtime_status_param,
                "Preparing " + std::string(quality_mode_label(requested_quality_mode)) + "...");
            auto prepare_result = data->runtime_client->prepare_session(
                build_prepare_request(requested_device, selection, requested_quality_mode),
                [&](const StageTiming& timing) {
                    log_stage_timing("ensure_engine_for_quality", kQualitySwitchPhase,
                                     requested_device, selection.executable_model_path,
                                     requested_resolution, selection.effective_resolution, timing);
                });
            if (!prepare_result) {
                data->last_error = "Failed to prepare runtime session for " +
                                   std::string(quality_mode_label(requested_quality_mode)) +
                                   " using " + selection.executable_model_path.filename().string() +
                                   ": " + prepare_result.error().message;
                log_engine_event("ensure_engine_for_quality", "engine_create_error",
                                 kQualitySwitchPhase, requested_device, requested_device,
                                 selection.executable_model_path, requested_resolution,
                                 selection.effective_resolution, std::nullopt,
                                 prepare_result.error().message);
                log_message("ensure_engine_for_quality", data->last_error);
                // Continue to the next lower resolution candidate. For fixed quality modes this
                // is a compile failure fallback, not a missing-artifact case -- the user will see
                // a Note in the status panel once the fallback resolution succeeds.
                continue;
            }
            effective_device = prepare_result->session.effective_device;
            fallback = prepare_result->session.backend_fallback;
            log_message("ensure_engine_for_quality",
                        "Runtime session prepared. reused_existing_session=" +
                            std::to_string(prepare_result->session.reused_existing_session) +
                            " ref_count=" + std::to_string(prepare_result->session.ref_count));
        } else {
            set_string_param_value(
                data->runtime_status_param,
                "Preparing " + std::string(quality_mode_label(requested_quality_mode)) + "...");
            auto engine_result = Engine::create(
                selection.executable_model_path, requested_device,
                [&](const StageTiming& timing) {
                    log_stage_timing("ensure_engine_for_quality", kQualitySwitchPhase,
                                     requested_device, selection.executable_model_path,
                                     requested_resolution, selection.effective_resolution, timing);
                },
                ofx_engine_options(requested_device));
            if (!engine_result) {
                data->last_error = "Failed to create engine for " +
                                   std::string(quality_mode_label(requested_quality_mode)) +
                                   " using " + selection.executable_model_path.filename().string() +
                                   ": " + engine_result.error().message;
                log_engine_event("ensure_engine_for_quality", "engine_create_error",
                                 kQualitySwitchPhase, requested_device, requested_device,
                                 selection.executable_model_path, requested_resolution,
                                 selection.effective_resolution, std::nullopt,
                                 engine_result.error().message);
                log_message("ensure_engine_for_quality", data->last_error);
                // Continue to the next lower resolution candidate (same rationale as above).
                continue;
            }

            auto engine = std::move(*engine_result);
            effective_device = engine->current_device();
            fallback = engine->backend_fallback();
            if (!backend_matches_request(*engine, requested_device)) {
                data->last_error = backend_mismatch_message(
                    "Quality switch", requested_device, *engine, selection.executable_model_path,
                    engine->backend_fallback());
                log_message("ensure_engine_for_quality", data->last_error);
                if (is_fixed_quality_mode(requested_quality_mode) &&
                    !cpu_quality_guardrail_active) {
                    update_runtime_panel(data);
                    log_quality_total("backend_mismatch", data->last_error);
                    return false;
                }
                continue;
            }
            data->engine = std::move(engine);
        }

        log_engine_event("ensure_engine_for_quality", "engine_create_result", kQualitySwitchPhase,
                         requested_device, effective_device, selection.executable_model_path,
                         requested_resolution, selection.effective_resolution, fallback);
        if (!backend_matches_request(effective_device, requested_device)) {
            data->last_error =
                "Quality switch requested backend " + backend_label(requested_device.backend) +
                " for " + selection.executable_model_path.filename().string() +
                " but the runtime is using " + backend_label(effective_device.backend) + ".";
            if (fallback.has_value() && !fallback->reason.empty()) {
                data->last_error += " Reason: " + fallback->reason;
            }
            log_message("ensure_engine_for_quality", data->last_error);
            if (is_fixed_quality_mode(requested_quality_mode) && !cpu_quality_guardrail_active) {
                update_runtime_panel(data);
                log_quality_total("backend_mismatch", data->last_error);
                return false;
            }
            continue;
        }

        data->device = effective_device;
        data->model_path = selection.executable_model_path;
        data->cached_result_valid = false;
        data->cached_signature = 0;
        data->cached_signature_valid = false;
        data->active_quality_mode = requested_quality_mode;
        data->requested_resolution = requested_resolution;
        data->active_resolution = selection.effective_resolution;
        data->cpu_quality_guardrail_active = cpu_quality_guardrail_active;
        data->render_count = 0;
        data->last_error.clear();
        data->last_frame_ms = 0.0;
        data->avg_frame_ms = 0.0;
        data->frame_time_samples = 0;
        if (selection.used_fallback) {
            const std::string fallback_note =
                std::string(quality_mode_label(requested_quality_mode)) + " (" +
                std::to_string(selection.requested_resolution) +
                "px) unavailable on this hardware -- using " +
                std::to_string(selection.effective_resolution) + "px";
            data->last_warning = fallback_note;
            log_message("ensure_engine_for_quality", "fallback_note=" + fallback_note);
        } else {
            data->last_warning.clear();
        }
        if (cpu_quality_guardrail_active) {
            log_message(
                "ensure_engine_for_quality",
                "CPU guardrail active: " + std::string(quality_mode_label(requested_quality_mode)) +
                    " is capped to Draft (512).");
        }
        log_message("ensure_engine_for_quality",
                    std::string("Switched to artifact ") +
                        selection.executable_model_path.filename().string());
        log_message("ensure_engine_for_quality",
                    std::string("Effective backend: ") + backend_label(data->device.backend));
        log_message("ensure_engine_for_quality",
                    "Effective resolution: " + std::to_string(data->active_resolution));
        update_runtime_panel_values(data);
        log_quality_total("success");
        return true;
    }

    log_quality_total("exhausted_candidates", data->last_error);
    update_runtime_panel(data);
    return false;
}

void update_runtime_panel(InstanceData* data) {
    if (data == nullptr) {
        return;
    }
    data->runtime_panel_dirty = true;
    if (data->in_render) {
        return;
    }
    data->runtime_panel_dirty = false;
    update_runtime_panel_values(data);
}

void flush_runtime_panel(InstanceData* data) {
    if (data != nullptr && data->runtime_panel_dirty && !data->in_render) {
        data->runtime_panel_dirty = false;
        update_runtime_panel_values(data);
    }
}

void set_param_enabled(OfxParamHandle param, bool enabled) {
    if (param == nullptr) {
        return;
    }
    OfxPropertySetHandle props = nullptr;
    if (g_suites.parameter->paramGetPropertySet(param, &props) == kOfxStatOK) {
        g_suites.property->propSetInt(props, kOfxParamPropEnabled, 0, enabled ? 1 : 0);
    }
}

void sync_dependent_params(InstanceData* data) {
    int tiling_enabled = 0;
    if (data->enable_tiling_param) {
        g_suites.parameter->paramGetValue(data->enable_tiling_param, &tiling_enabled);
    }
    set_param_enabled(data->tile_overlap_param, tiling_enabled != 0);

    int despeckle_enabled = 0;
    if (data->despeckle_param) {
        g_suites.parameter->paramGetValue(data->despeckle_param, &despeckle_enabled);
    }
    set_param_enabled(data->despeckle_size_param, despeckle_enabled != 0);

    int source_passthrough = 0;
    if (data->source_passthrough_param) {
        g_suites.parameter->paramGetValue(data->source_passthrough_param, &source_passthrough);
    }
    set_param_enabled(data->edge_erode_param, source_passthrough != 0);
    set_param_enabled(data->edge_blur_param, source_passthrough != 0);
}

OfxStatus instance_changed(OfxImageEffectHandle instance, OfxPropertySetHandle in_args) {
    InstanceData* data = get_instance_data(instance);
    if (data == nullptr) {
        return kOfxStatReplyDefault;
    }
    if (in_args != nullptr && g_suites.property != nullptr) {
        std::string changed_param;
        if (get_string(in_args, kOfxPropName, changed_param)) {
            if (changed_param == kParamEnableTiling || changed_param == kParamAutoDespeckle ||
                changed_param == kParamSourcePassthrough) {
                sync_dependent_params(data);
            }
            if (changed_param == kParamRenderTimeout || changed_param == kParamPrepareTimeout) {
                if (data->runtime_client) {
                    int render_t = 30;
                    int prepare_t = 120;
                    if (data->render_timeout_param) {
                        g_suites.parameter->paramGetValue(data->render_timeout_param, &render_t);
                    }
                    if (data->prepare_timeout_param) {
                        g_suites.parameter->paramGetValue(data->prepare_timeout_param, &prepare_t);
                    }
                    data->runtime_client->set_request_timeout_ms(render_t * 1000);
                    data->runtime_client->set_prepare_timeout_ms(prepare_t * 1000);
                }
            }
            if (changed_param == kParamQuantizationMode) {
                int quant = 0;
                if (data->quantization_mode_param &&
                    g_suites.parameter->paramGetValue(data->quantization_mode_param, &quant) ==
                        kOfxStatOK) {
                    DeviceInfo requested_device = data->preferred_device;
                    if (requested_device.backend == Backend::Auto) {
                        requested_device = data->device;
                    }
                    if (requested_device.backend == Backend::TensorRT &&
                        quant == kQuantizationInt8) {
                        data->last_error =
                            "INT8 (Compact) is not supported by the "
                            "TensorRT RTX provider. Please use FP16 (Full).";
                        data->quantization_error_active = true;
                        data->runtime_panel_dirty = true;
                    } else if (data->quantization_error_active) {
                        data->last_error.clear();
                        data->quantization_error_active = false;
                        data->runtime_panel_dirty = true;
                    }
                }
            }
        }
        std::string change_reason;
        if (get_string(in_args, kOfxPropChangeReason, change_reason) &&
            change_reason == kOfxChangePluginEdited) {
            return kOfxStatOK;
        }
    }
    if (data->runtime_panel_dirty && !data->in_render) {
        data->runtime_panel_dirty = false;
        update_runtime_panel_values(data);
    }
    return kOfxStatOK;
}

OfxStatus destroy_instance(OfxImageEffectHandle instance) {
    InstanceData* data = get_instance_data(instance);
    if (data != nullptr) {
        delete data;
        set_instance_data(instance, nullptr);
    }
    log_message("destroy_instance", "Instance destroyed.");
    return kOfxStatOK;
}

}  // namespace corridorkey::ofx
