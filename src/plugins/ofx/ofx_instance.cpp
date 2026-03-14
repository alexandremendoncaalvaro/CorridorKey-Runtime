#include "ofx_shared.hpp"

#include <corridorkey/engine.hpp>

#include <filesystem>
#include <new>

#include "app/runtime_contracts.hpp"
#include "common/runtime_paths.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace corridorkey::ofx {

namespace {

std::optional<std::filesystem::path> plugin_module_path() {
#ifdef _WIN32
    HMODULE module = nullptr;
    auto address = reinterpret_cast<LPCWSTR>(&plugin_module_path);
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            address, &module)) {
        return std::nullopt;
    }

    std::wstring buffer(MAX_PATH, L'\0');
    DWORD length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) {
        return std::nullopt;
    }
    buffer.resize(length);
    return std::filesystem::path(buffer);
#else
    return std::nullopt;
#endif
}

std::filesystem::path resolve_models_root() {
    if (auto override_path = common::environment_variable_copy("CORRIDORKEY_MODELS_DIR");
        override_path.has_value()) {
        return std::filesystem::path(*override_path);
    }

    if (auto module_path = plugin_module_path(); module_path.has_value()) {
        auto resources = module_path->parent_path().parent_path() / "Resources" / "models";
        std::error_code error;
        if (std::filesystem::exists(resources, error) && !error) {
            return resources;
        }
    }

    return common::default_models_root();
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
        return kOfxStatErrMissingHostFeature;
    }

    auto data = std::unique_ptr<InstanceData>(new (std::nothrow) InstanceData());
    if (!data) {
        return kOfxStatErrMemory;
    }
    data->effect = instance;

    if (g_suites.image_effect->clipGetHandle(instance, "Source", &data->source_clip, nullptr) !=
        kOfxStatOK) {
        return kOfxStatFailed;
    }

    if (g_suites.image_effect->clipGetHandle(instance, "Output", &data->output_clip, nullptr) !=
        kOfxStatOK) {
        return kOfxStatFailed;
    }

    OfxParamSetHandle param_set;
    if (g_suites.image_effect->getParamSet(instance, &param_set) != kOfxStatOK) {
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

    auto capabilities = runtime_capabilities();
    auto preset = app::default_preset_for_capabilities(capabilities);
    auto model_entry = app::default_model_for_request(capabilities, data->device, preset);
    if (!model_entry.has_value()) {
        post_message(kOfxMessageError, "No compatible model found for this device.", instance);
        return kOfxStatFailed;
    }

    auto models_root = resolve_models_root();
    data->model_path = models_root / model_entry->filename;
    if (!std::filesystem::exists(data->model_path)) {
        post_message(kOfxMessageError, "Model file not found.", instance);
        return kOfxStatFailed;
    }

    auto engine_result = Engine::create(data->model_path, data->device);
    if (!engine_result) {
        post_message(kOfxMessageError,
                     ("Failed to load AI engine: " + engine_result.error().message).c_str(),
                     instance);
        return kOfxStatFailed;
    }

    data->engine = std::move(*engine_result);

    set_instance_data(instance, data.release());
    return kOfxStatOK;
}

OfxStatus destroy_instance(OfxImageEffectHandle instance) {
    InstanceData* data = get_instance_data(instance);
    if (data != nullptr) {
        delete data;
        set_instance_data(instance, nullptr);
    }
    return kOfxStatOK;
}

}  // namespace corridorkey::ofx
