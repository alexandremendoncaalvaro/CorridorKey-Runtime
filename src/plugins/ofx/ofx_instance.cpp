#include <algorithm>
#include <corridorkey/engine.hpp>
#include <filesystem>
#include <new>
#include <string>

#include "app/runtime_contracts.hpp"
#include "common/runtime_paths.hpp"
#include "ofx_logging.hpp"
#include "ofx_model_selection.hpp"
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

EngineCreateOptions ofx_engine_options(const DeviceInfo& requested_device) {
    EngineCreateOptions options;
    if (requested_device.backend != Backend::CPU) {
        options.allow_cpu_fallback = false;
        options.disable_cpu_ep_fallback = true;
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

std::string render_pass_label(std::uint64_t render_count) {
    return render_count == 0 ? "first_frame" : "subsequent_frame";
}

void log_stage_timing(std::string_view scope, std::string_view phase,
                      const DeviceInfo& requested_device,
                      const std::filesystem::path& artifact_path, int requested_resolution,
                      int effective_resolution, const StageTiming& timing) {
    log_message(scope, "event=stage phase=" + std::string(phase) + " stage=" + timing.name +
                           " total_ms=" + std::to_string(timing.total_ms) +
                           " requested_backend=" + backend_label(requested_device.backend) +
                           " artifact=" + artifact_path.filename().string() +
                           " requested_resolution=" +
                           std::to_string(requested_resolution) + " effective_resolution=" +
                           std::to_string(effective_resolution));
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
                          " effective_device=" + effective_device.name + " artifact=" +
                          artifact_path.filename().string() + " requested_resolution=" +
                          std::to_string(requested_resolution) + " effective_resolution=" +
                          std::to_string(effective_resolution);
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

    set_string_param_value(data->runtime_processing_param,
                           processing_backend_label(data->device.backend));
    set_string_param_value(data->runtime_device_param, processing_device_label(data->device));
    set_string_param_value(
        data->runtime_requested_quality_param,
        requested_quality_label(data->active_quality_mode, data->requested_resolution,
                                data->cpu_quality_guardrail_active));
    set_string_param_value(data->runtime_effective_quality_param,
                           effective_quality_label(data->active_resolution));
    set_string_param_value(data->runtime_artifact_param,
                           runtime_artifact_label(data->model_path));
}

void set_runtime_panel_status(InstanceData* data, const std::string& processing,
                              const std::string& device,
                              const std::string& requested_quality,
                              const std::string& effective_quality,
                              const std::string& artifact) {
    if (data == nullptr) {
        return;
    }

    set_string_param_value(data->runtime_processing_param, processing);
    set_string_param_value(data->runtime_device_param, device);
    set_string_param_value(data->runtime_requested_quality_param, requested_quality);
    set_string_param_value(data->runtime_effective_quality_param, effective_quality);
    set_string_param_value(data->runtime_artifact_param, artifact);
}

bool backend_matches_request(const Engine& engine, const DeviceInfo& requested_device) {
    if (requested_device.backend == Backend::CPU) {
        return true;
    }
    return engine.current_device().backend == requested_device.backend;
}

std::string backend_mismatch_message(const std::string& operation, const DeviceInfo& requested_device,
                                     const Engine& engine,
                                     const std::filesystem::path& artifact_path) {
    return operation + " requested backend " + backend_label(requested_device.backend) +
           " for " + artifact_path.filename().string() + " but the engine is using " +
           backend_label(engine.current_device().backend) + ".";
}

std::string backend_mismatch_message(const std::string& operation, const DeviceInfo& requested_device,
                                     const Engine& engine,
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

OfxStatus create_instance(OfxImageEffectHandle instance) {
    if (g_suites.image_effect == nullptr || g_suites.parameter == nullptr) {
        log_message("create_instance", "Missing required suites.");
        return kOfxStatErrMissingHostFeature;
    }

    auto data = std::unique_ptr<InstanceData>(new (std::nothrow) InstanceData());
    if (!data) {
        log_message("create_instance", "Failed to allocate InstanceData.");
        return kOfxStatErrMemory;
    }
    data->effect = instance;
    data->cpu_quality_guardrail_active = false;
    data->render_count = 0;

    if (g_suites.image_effect->clipGetHandle(instance, "Source", &data->source_clip, nullptr) !=
        kOfxStatOK) {
        log_message("create_instance", "Failed to get Source clip handle.");
        return kOfxStatFailed;
    }

    g_suites.image_effect->clipGetHandle(instance, kClipAlphaHint, &data->alpha_hint_clip, nullptr);

    if (g_suites.image_effect->clipGetHandle(instance, "Output", &data->output_clip, nullptr) !=
        kOfxStatOK) {
        log_message("create_instance", "Failed to get Output clip handle.");
        return kOfxStatFailed;
    }

    OfxParamSetHandle param_set;
    if (g_suites.image_effect->getParamSet(instance, &param_set) != kOfxStatOK) {
        log_message("create_instance", "Failed to get param set.");
        return kOfxStatFailed;
    }

    g_suites.parameter->paramGetHandle(param_set, kParamQualityMode, &data->quality_mode_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamOutputMode, &data->output_mode_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamDespillStrength, &data->despill_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamAutoDespeckle, &data->despeckle_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamDespeckleSize, &data->despeckle_size_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRefinerScale, &data->refiner_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamInputIsLinear, &data->input_is_linear_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamAlphaBlackPoint,
                                       &data->alpha_black_point_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamAlphaWhitePoint,
                                       &data->alpha_white_point_param, nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamAlphaErode, &data->alpha_erode_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamAlphaSoftness, &data->alpha_softness_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamBrightness, &data->brightness_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamSaturation, &data->saturation_param,
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

    set_runtime_panel_status(data.get(), "Initializing...", "Detecting...", "Initializing...",
                             "Not loaded", "Not loaded");

    data->models_root = resolve_models_root();
    DeviceInfo detected_device = auto_detect();
    log_message("create_instance", std::string("Detected device: ") + detected_device.name);
    log_message("create_instance",
                std::string("Detected backend: ") + backend_label(detected_device.backend));
    auto capabilities = runtime_capabilities();
    log_message("create_instance", std::string("Platform: ") + capabilities.platform);

    auto bootstrap_candidates =
        build_bootstrap_candidates(capabilities, detected_device, data->models_root);
    if (bootstrap_candidates.empty()) {
        log_message("create_instance", "No compatible model artifacts found for OFX bootstrap.");
        post_message(kOfxMessageError, "No compatible model artifacts found for this device.",
                     instance);
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
        auto engine_result = Engine::create(
            candidate.executable_model_path, candidate.device,
            [&](const StageTiming& timing) {
                log_stage_timing("create_instance", kBootstrapPhase, candidate.device,
                                 candidate.executable_model_path, candidate.requested_resolution,
                                 candidate.effective_resolution, timing);
            },
            ofx_engine_options(candidate.device));
        if (!engine_result) {
            std::string failure =
                backend_label(candidate.device.backend) + ": " + engine_result.error().message;
            log_engine_event("create_instance", "engine_create_error", kBootstrapPhase,
                             candidate.device, candidate.device, candidate.executable_model_path,
                             candidate.requested_resolution, candidate.effective_resolution,
                             std::nullopt, engine_result.error().message);
            log_message("create_instance", std::string("Engine create failed: ") + failure);
            if (!failure_summary.empty()) {
                failure_summary += " | ";
            }
            failure_summary += failure;
            continue;
        }

        auto engine = std::move(*engine_result);
        log_engine_event("create_instance", "engine_create_result", kBootstrapPhase,
                         candidate.device, engine->current_device(),
                         candidate.executable_model_path, candidate.requested_resolution,
                         candidate.effective_resolution, engine->backend_fallback());
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
        data->preferred_device = candidate.device;
        data->device = data->engine->current_device();
        data->model_path = candidate.executable_model_path;
        data->active_quality_mode = kQualityAuto;
        data->requested_resolution = candidate.requested_resolution;
        data->active_resolution = candidate.effective_resolution;
        data->cpu_quality_guardrail_active = false;
        data->render_count = 0;
        data->last_error.clear();

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
        return kOfxStatOK;
    }

    data->last_error = failure_summary;
    std::string failure_message = "Failed to load AI engine: " + failure_summary;
    post_message(kOfxMessageError, failure_message.c_str(), instance);
    return kOfxStatFailed;
}

bool ensure_engine_for_quality(InstanceData* data, int quality_mode, int input_width,
                               int input_height) {
    if (data == nullptr || data->engine == nullptr) {
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
    auto selections = quality_artifact_candidates(data->models_root, requested_device.backend,
                                                  effective_quality_mode, input_width,
                                                  input_height);
    data->cpu_quality_guardrail_active = cpu_quality_guardrail_active;
    if (cpu_quality_guardrail_active) {
        data->last_error = "CPU backend is limited to Preview (512) for interactive rendering. " +
                           std::string(quality_mode_label(requested_quality_mode)) +
                           " will run as Preview (512).";
        log_message("ensure_engine_for_quality", data->last_error);
    }
    if (selections.empty()) {
        if (cpu_quality_guardrail_active) {
            data->last_error =
                "CPU backend is limited to Preview (512), but the 512 artifact is missing for " +
                backend_label(requested_device.backend) + ".";
        } else {
            data->last_error =
                "Requested quality " + std::string(quality_mode_label(requested_quality_mode)) +
                " requires a " + std::to_string(requested_resolution) +
                "px artifact for backend " + backend_label(requested_device.backend) +
                ", but that artifact is missing.";
        }
        log_message("ensure_engine_for_quality", data->last_error);
        return false;
    }

    data->device = data->engine->current_device();
    bool current_backend_matches = backend_matches_request(*data->engine, requested_device);

    for (const auto& selection : selections) {
        if (current_backend_matches && selection.executable_model_path == data->model_path &&
            selection.effective_resolution == data->active_resolution) {
            data->active_quality_mode = requested_quality_mode;
            data->requested_resolution = requested_resolution;
            data->active_resolution = selection.effective_resolution;
            data->last_error.clear();
            update_runtime_panel_values(data);
            return true;
        }

        constexpr std::string_view kQualitySwitchPhase = "quality_switch";
        log_engine_event("ensure_engine_for_quality", "engine_create_begin", kQualitySwitchPhase,
                         requested_device, requested_device, selection.executable_model_path,
                         requested_resolution, selection.effective_resolution);
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
                               std::string(quality_mode_label(requested_quality_mode)) + " using " +
                               selection.executable_model_path.filename().string() + ": " +
                               engine_result.error().message;
            log_engine_event("ensure_engine_for_quality", "engine_create_error",
                             kQualitySwitchPhase, requested_device, requested_device,
                             selection.executable_model_path, requested_resolution,
                             selection.effective_resolution, std::nullopt,
                             engine_result.error().message);
            log_message("ensure_engine_for_quality", data->last_error);
            if (is_fixed_quality_mode(requested_quality_mode) && !cpu_quality_guardrail_active) {
                return false;
            }
            continue;
        }

        auto engine = std::move(*engine_result);
        log_engine_event("ensure_engine_for_quality", "engine_create_result",
                         kQualitySwitchPhase, requested_device, engine->current_device(),
                         selection.executable_model_path, requested_resolution,
                         selection.effective_resolution, engine->backend_fallback());
        if (!backend_matches_request(*engine, requested_device)) {
            data->last_error = backend_mismatch_message(
                "Quality switch", requested_device, *engine, selection.executable_model_path,
                engine->backend_fallback());
            log_message("ensure_engine_for_quality", data->last_error);
            if (is_fixed_quality_mode(requested_quality_mode) && !cpu_quality_guardrail_active) {
                return false;
            }
            continue;
        }

        data->engine = std::move(engine);
        data->device = data->engine->current_device();
        data->model_path = selection.executable_model_path;
        data->active_quality_mode = requested_quality_mode;
        data->requested_resolution = requested_resolution;
        data->active_resolution = selection.effective_resolution;
        data->cpu_quality_guardrail_active = cpu_quality_guardrail_active;
        data->render_count = 0;
        data->last_error.clear();
        if (selection.used_fallback) {
            log_message("ensure_engine_for_quality",
                        "Auto quality requested " + std::to_string(selection.requested_resolution) +
                            " and fell back to " +
                            std::to_string(selection.effective_resolution));
        }
        if (cpu_quality_guardrail_active) {
            log_message("ensure_engine_for_quality",
                        "CPU guardrail active: " +
                            std::string(quality_mode_label(requested_quality_mode)) +
                            " is capped to Preview (512).");
        }
        log_message("ensure_engine_for_quality",
                    std::string("Switched to artifact ") +
                        selection.executable_model_path.filename().string());
        log_message("ensure_engine_for_quality",
                    std::string("Effective backend: ") + backend_label(data->device.backend));
        log_message("ensure_engine_for_quality",
                    "Effective resolution: " + std::to_string(data->active_resolution));
        update_runtime_panel_values(data);
        return true;
    }

    return false;
}

void update_runtime_panel(InstanceData* data) {
    update_runtime_panel_values(data);
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
