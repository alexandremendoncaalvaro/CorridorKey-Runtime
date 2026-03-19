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

std::string runtime_artifact_label(const std::filesystem::path& model_path, int resolution) {
    if (model_path.empty()) {
        return "Not loaded";
    }

    std::string label = model_path.filename().string();
    if (resolution > 0) {
        label += " (" + std::to_string(resolution) + "px)";
    }
    return label;
}

void set_string_param_value(OfxParamHandle param, const std::string& value) {
    if (g_suites.parameter == nullptr || param == nullptr) {
        return;
    }
    g_suites.parameter->paramSetValue(param, value.c_str());
}

void update_runtime_panel(InstanceData* data) {
    if (data == nullptr) {
        return;
    }

    set_string_param_value(data->runtime_processing_param,
                           processing_backend_label(data->device.backend));
    set_string_param_value(data->runtime_device_param, processing_device_label(data->device));
    set_string_param_value(data->runtime_artifact_param,
                           runtime_artifact_label(data->model_path, data->active_resolution));
}

void set_runtime_panel_status(InstanceData* data, const std::string& processing,
                              const std::string& device, const std::string& artifact) {
    if (data == nullptr) {
        return;
    }

    set_string_param_value(data->runtime_processing_param, processing);
    set_string_param_value(data->runtime_device_param, device);
    set_string_param_value(data->runtime_artifact_param, artifact);
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
    g_suites.parameter->paramGetHandle(param_set, kParamRuntimeArtifact,
                                       &data->runtime_artifact_param, nullptr);

    set_runtime_panel_status(data.get(), "Initializing...", "Detecting...", "Not loaded");

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
        log_message("create_instance",
                    std::string("Attempting backend: ") + backend_label(candidate.device.backend));
        log_message("create_instance", std::string("Requested model: ") +
                                           candidate.requested_model_path.filename().string());
        if (candidate.executable_model_path != candidate.requested_model_path) {
            log_message("create_instance", std::string("Requested executable artifact: ") +
                                               candidate.executable_model_path.filename().string());
        }

        auto engine_result = Engine::create(candidate.executable_model_path, candidate.device);
        if (!engine_result) {
            std::string failure =
                backend_label(candidate.device.backend) + ": " + engine_result.error().message;
            log_message("create_instance", std::string("Engine create failed: ") + failure);
            if (!failure_summary.empty()) {
                failure_summary += " | ";
            }
            failure_summary += failure;
            continue;
        }

        data->engine = std::move(*engine_result);
        data->device = data->engine->current_device();
        data->model_path = candidate.executable_model_path;
        data->active_quality_mode = kQualityAuto;
        data->active_resolution = data->engine->recommended_resolution();
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
        update_runtime_panel(data.get());

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
    if (data == nullptr) return true;

    auto selection = select_quality_artifact(data->models_root, data->device.backend, quality_mode,
                                             input_width, input_height);
    if (!selection.has_value()) {
        int requested_resolution =
            resolve_target_resolution(quality_mode, input_width, input_height);
        data->last_error = "Requested quality " + std::string(quality_mode_label(quality_mode)) +
                           " requires a " + std::to_string(requested_resolution) +
                           "px artifact for backend " + backend_label(data->device.backend) +
                           ", but that artifact is missing.";
        log_message("ensure_engine_for_quality", data->last_error);
        return false;
    }

    if (selection->effective_resolution == data->active_resolution &&
        selection->executable_model_path == data->model_path) {
        return true;
    }

    auto engine_result = Engine::create(selection->executable_model_path, data->device);
    if (!engine_result) {
        data->last_error = "Failed to create engine for " +
                           std::string(quality_mode_label(quality_mode)) + " using " +
                           selection->executable_model_path.filename().string() + ": " +
                           engine_result.error().message;
        log_message("ensure_engine_for_quality", data->last_error);
        return false;
    }

    data->engine = std::move(*engine_result);
    data->device = data->engine->current_device();
    data->model_path = selection->executable_model_path;
    data->active_quality_mode = quality_mode;
    data->active_resolution = selection->effective_resolution;
    data->last_error.clear();
    if (selection->used_fallback) {
        log_message("ensure_engine_for_quality",
                    "Auto quality requested " + std::to_string(selection->requested_resolution) +
                        " and fell back to " + std::to_string(selection->effective_resolution));
    }
    log_message("ensure_engine_for_quality",
                std::string("Switched to artifact ") +
                    selection->executable_model_path.filename().string());
    log_message("ensure_engine_for_quality",
                std::string("Effective backend: ") + backend_label(data->device.backend));
    log_message("ensure_engine_for_quality",
                "Effective resolution: " + std::to_string(data->active_resolution));
    update_runtime_panel(data);
    return true;
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
