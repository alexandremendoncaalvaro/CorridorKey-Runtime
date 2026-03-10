#include "runtime_contracts.hpp"

#include <algorithm>
#include <array>
#include <filesystem>

#include "../frame_io/video_io.hpp"

namespace corridorkey {

namespace {

ModelCatalogEntry make_model_entry(const std::string& variant, int resolution,
                                   const std::string& description, bool validated_for_macos,
                                   bool packaged_for_macos) {
    ModelCatalogEntry entry;
    entry.variant = variant;
    entry.resolution = resolution;
    entry.filename = "corridorkey_" + variant + "_" + std::to_string(resolution) + ".onnx";
    entry.description = description;
    entry.download_url = "https://huggingface.co/corridorkey/models/resolve/main/" + entry.filename;
    entry.validated_for_macos = validated_for_macos;
    entry.packaged_for_macos = packaged_for_macos;
    return entry;
}

}  // namespace

RuntimeCapabilities runtime_capabilities() {
    RuntimeCapabilities capabilities;

#if defined(__APPLE__)
    capabilities.platform = "macos";
#elif defined(_WIN32)
    capabilities.platform = "windows";
#else
    capabilities.platform = "linux";
#endif

    auto devices = list_devices();
    capabilities.supported_backends.reserve(devices.size());
    for (const auto& device : devices) {
        capabilities.supported_backends.push_back(device.backend);
        if (device.backend == Backend::CoreML) {
            capabilities.apple_silicon = true;
            capabilities.coreml_available = true;
        }
        if (device.backend == Backend::CPU) {
            capabilities.cpu_fallback_available = true;
        }
    }

    capabilities.videotoolbox_available = is_videotoolbox_available();
    capabilities.default_video_encoder = default_video_encoder_for_path("output.mp4");

    return capabilities;
}

std::vector<ModelCatalogEntry> model_catalog() {
    return {
        make_model_entry("int8", 512, "Validated macOS default for preview and 8 GB machines.",
                         true, true),
        make_model_entry("int8", 768, "Validated macOS default for 16 GB Apple Silicon systems.",
                         true, true),
        make_model_entry("int8", 1024, "Available for manual testing on high-memory systems.",
                         false, false),
        make_model_entry("fp16", 512, "GPU-focused reference variant for non-macOS expansion.",
                         false, false),
        make_model_entry("fp16", 768, "Higher quality GPU-focused reference variant.", false,
                         false),
        make_model_entry("fp16", 1024, "Maximum GPU-focused reference variant.", false, false),
        make_model_entry("fp32", 512, "Reference validation variant.", false, false),
        make_model_entry("fp32", 768, "Higher resolution reference validation variant.", false,
                         false),
        make_model_entry("fp32", 1024, "Maximum resolution reference validation variant.", false,
                         false),
    };
}

std::vector<PresetDefinition> preset_catalog() {
    return {
        PresetDefinition{
            "mac-preview",
            "Mac Preview",
            "Fast validation preset for smoke tests and low-memory systems.",
            InferenceParams{512, 1.0F, true, 400, 1.0F, false, 1, false, 32},
            "corridorkey_int8_512.onnx",
            false,
        },
        PresetDefinition{
            "mac-balanced",
            "Mac Balanced",
            "Default Apple Silicon preset with automatic resolution and tiling support.",
            InferenceParams{0, 1.0F, true, 400, 1.0F, false, 1, true, 32},
            "corridorkey_int8_768.onnx",
            true,
        },
        PresetDefinition{
            "mac-max-quality",
            "Mac Max Quality",
            "Validated high-quality preset for larger inputs using tiled inference.",
            InferenceParams{768, 1.0F, true, 400, 1.0F, false, 1, true, 64},
            "corridorkey_int8_768.onnx",
            false,
        },
    };
}

}  // namespace corridorkey

namespace corridorkey::app {

std::string backend_to_string(Backend backend) {
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
        default:
            return "auto";
    }
}

std::string job_event_type_to_string(JobEventType type) {
    switch (type) {
        case JobEventType::JobStarted:
            return "job_started";
        case JobEventType::BackendSelected:
            return "backend_selected";
        case JobEventType::Progress:
            return "progress";
        case JobEventType::Warning:
            return "warning";
        case JobEventType::ArtifactWritten:
            return "artifact_written";
        case JobEventType::Completed:
            return "completed";
        case JobEventType::Failed:
            return "failed";
        case JobEventType::Cancelled:
            return "cancelled";
    }

    return "progress";
}

nlohmann::json to_json(const Error& error) {
    nlohmann::json json;
    json["code"] = static_cast<int>(error.code);
    json["message"] = error.message;
    return json;
}

nlohmann::json to_json(const BackendFallbackInfo& fallback) {
    nlohmann::json json;
    json["requested_backend"] = backend_to_string(fallback.requested_backend);
    json["selected_backend"] = backend_to_string(fallback.selected_backend);
    json["reason"] = fallback.reason;
    return json;
}

nlohmann::json to_json(const RuntimeCapabilities& capabilities) {
    nlohmann::json json;
    json["platform"] = capabilities.platform;
    json["apple_silicon"] = capabilities.apple_silicon;
    json["coreml_available"] = capabilities.coreml_available;
    json["cpu_fallback_available"] = capabilities.cpu_fallback_available;
    json["videotoolbox_available"] = capabilities.videotoolbox_available;
    json["tiling_supported"] = capabilities.tiling_supported;
    json["batching_supported"] = capabilities.batching_supported;
    json["default_video_encoder"] = capabilities.default_video_encoder;

    nlohmann::json backends = nlohmann::json::array();
    for (Backend backend : capabilities.supported_backends) {
        backends.push_back(backend_to_string(backend));
    }
    json["supported_backends"] = backends;

    return json;
}

nlohmann::json to_json(const JobEvent& event) {
    nlohmann::json json;
    json["type"] = job_event_type_to_string(event.type);
    json["phase"] = event.phase;
    json["progress"] = event.progress;
    if (event.backend != Backend::Auto) {
        json["backend"] = backend_to_string(event.backend);
    }
    if (!event.message.empty()) {
        json["message"] = event.message;
    }
    if (!event.artifact_path.empty()) {
        json["artifact_path"] = event.artifact_path;
    }
    if (event.error.has_value()) {
        json["error"] = to_json(*event.error);
    }
    if (event.fallback.has_value()) {
        json["fallback"] = to_json(*event.fallback);
    }
    return json;
}

nlohmann::json to_json(const ModelCatalogEntry& model) {
    nlohmann::json json;
    json["variant"] = model.variant;
    json["resolution"] = model.resolution;
    json["filename"] = model.filename;
    json["description"] = model.description;
    json["download_url"] = model.download_url;
    json["validated_for_macos"] = model.validated_for_macos;
    json["packaged_for_macos"] = model.packaged_for_macos;
    return json;
}

nlohmann::json to_json(const PresetDefinition& preset) {
    nlohmann::json params;
    params["target_resolution"] = preset.params.target_resolution;
    params["despill_strength"] = preset.params.despill_strength;
    params["auto_despeckle"] = preset.params.auto_despeckle;
    params["despeckle_size"] = preset.params.despeckle_size;
    params["refiner_scale"] = preset.params.refiner_scale;
    params["input_is_linear"] = preset.params.input_is_linear;
    params["batch_size"] = preset.params.batch_size;
    params["enable_tiling"] = preset.params.enable_tiling;
    params["tile_padding"] = preset.params.tile_padding;

    nlohmann::json json;
    json["id"] = preset.id;
    json["name"] = preset.name;
    json["description"] = preset.description;
    json["recommended_model"] = preset.recommended_model;
    json["default_for_macos"] = preset.default_for_macos;
    json["params"] = params;
    return json;
}

}  // namespace corridorkey::app
