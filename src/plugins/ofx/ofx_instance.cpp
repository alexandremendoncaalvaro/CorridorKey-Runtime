#include <corridorkey/engine.hpp>
#include <filesystem>
#include <new>

#include "app/runtime_contracts.hpp"
#include "common/runtime_paths.hpp"
#include "ofx_logging.hpp"
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

    g_suites.parameter->paramGetHandle(param_set, kParamDespillStrength, &data->despill_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamAutoDespeckle, &data->despeckle_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamRefinerScale, &data->refiner_param,
                                       nullptr);
    g_suites.parameter->paramGetHandle(param_set, kParamInputIsLinear, &data->input_is_linear_param,
                                       nullptr);

    data->device = auto_detect();
#if defined(__APPLE__)
    data->device.backend = Backend::Auto;
#endif
    log_message("create_instance", std::string("Detected device: ") + data->device.name);
    log_message("create_instance",
                std::string("Requested backend: ") + backend_label(data->device.backend));

    auto capabilities = runtime_capabilities();
    log_message("create_instance", std::string("Platform: ") + capabilities.platform);
    auto preset = app::default_preset_for_capabilities(capabilities);
    auto model_entry = app::default_model_for_request(capabilities, data->device, preset);
    if (!model_entry.has_value()) {
        log_message("create_instance", "No compatible model found.");
        post_message(kOfxMessageError, "No compatible model found for this device.", instance);
        return kOfxStatFailed;
    }
    log_message("create_instance", std::string("Selected model: ") + model_entry->filename);

    auto models_root = resolve_models_root();
    data->model_path = models_root / model_entry->filename;
    if (!std::filesystem::exists(data->model_path)) {
        log_message("create_instance",
                    std::string("Model file missing: ") + data->model_path.string());
        post_message(kOfxMessageError, "Model file not found.", instance);
        return kOfxStatFailed;
    }

    auto engine_result = Engine::create(data->model_path, data->device);
    if (!engine_result) {
        log_message("create_instance",
                    std::string("Engine create failed: ") + engine_result.error().message);
        post_message(kOfxMessageError,
                     ("Failed to load AI engine: " + engine_result.error().message).c_str(),
                     instance);
        return kOfxStatFailed;
    }

    data->engine = std::move(*engine_result);
    log_message("create_instance", "Engine created successfully.");

    set_instance_data(instance, data.release());
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
