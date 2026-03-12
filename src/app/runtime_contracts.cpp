#include "runtime_contracts.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <utility>

#include "../core/mlx_probe.hpp"
#include "../frame_io/video_io.hpp"

namespace corridorkey {

namespace {

std::string normalized_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::optional<std::string> normalize_preset_selector(const std::string& selector) {
    if (selector.empty()) {
        return std::nullopt;
    }

    auto normalized = normalized_lower(selector);
    if (normalized == "preview") {
        return std::string("mac-preview");
    }
    if (normalized == "balanced" || normalized == "default") {
        return std::string("mac-balanced");
    }
    if (normalized == "max" || normalized == "max-quality") {
        return std::string("mac-max-quality");
    }
    return normalized;
}

ModelCatalogEntry make_model_entry(const std::string& variant, int resolution,
                                   const std::string& filename, const std::string& artifact_family,
                                   const std::string& recommended_backend,
                                   const std::string& description, const std::string& intended_use,
                                   bool validated_for_macos, bool packaged_for_macos,
                                   std::vector<std::string> validated_platforms,
                                   std::vector<std::string> intended_platforms,
                                   std::vector<std::string> validated_hardware_tiers) {
    ModelCatalogEntry entry;
    entry.variant = variant;
    entry.resolution = resolution;
    entry.filename = filename;
    entry.artifact_family = artifact_family;
    entry.recommended_backend = recommended_backend;
    entry.description = description;
    entry.download_url = "https://huggingface.co/corridorkey/models/resolve/main/" + entry.filename;
    entry.intended_use = intended_use;
    entry.validated_for_macos = validated_for_macos;
    entry.packaged_for_macos = packaged_for_macos;
    entry.validated_platforms = std::move(validated_platforms);
    entry.intended_platforms = std::move(intended_platforms);
    entry.validated_hardware_tiers = std::move(validated_hardware_tiers);
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
        if (device.backend == Backend::MLX) {
            capabilities.apple_silicon = true;
        }
        if (device.backend == Backend::CPU) {
            capabilities.cpu_fallback_available = true;
        }
    }

    capabilities.mlx_probe_available = core::mlx_probe_available();
    capabilities.videotoolbox_available = is_videotoolbox_available();
    capabilities.default_video_encoder = default_video_encoder_for_path("output.mp4");

    return capabilities;
}

std::optional<ModelCatalogEntry> find_model_by_filename(const std::string& filename) {
    for (const auto& entry : model_catalog()) {
        if (entry.filename == filename) {
            return entry;
        }
    }

    return std::nullopt;
}

std::optional<PresetDefinition> find_preset_by_selector(const std::string& selector) {
    auto normalized = normalize_preset_selector(selector);
    if (!normalized.has_value()) {
        return std::nullopt;
    }

    for (const auto& preset : preset_catalog()) {
        if (normalized_lower(preset.id) == *normalized) {
            return preset;
        }
    }

    return std::nullopt;
}

std::optional<PresetDefinition> default_preset_for_capabilities(
    const RuntimeCapabilities& capabilities) {
    if (capabilities.platform == "macos" && capabilities.apple_silicon) {
        for (const auto& preset : preset_catalog()) {
            if (preset.default_for_macos) {
                return preset;
            }
        }
    }

    return std::nullopt;
}

std::optional<ModelCatalogEntry> default_model_for_request(
    const RuntimeCapabilities& capabilities, Backend requested_backend,
    const std::optional<PresetDefinition>& preset) {
    if (requested_backend == Backend::CPU || !capabilities.apple_silicon) {
        return find_model_by_filename("corridorkey_int8_512.onnx");
    }

    if ((requested_backend == Backend::Auto || requested_backend == Backend::MLX) &&
        capabilities.platform == "macos" && capabilities.apple_silicon) {
        if (preset.has_value()) {
            auto preset_model = find_model_by_filename(preset->recommended_model);
            if (preset_model.has_value()) {
                return preset_model;
            }
        }
        return find_model_by_filename("corridorkey_mlx.safetensors");
    }

    return find_model_by_filename("corridorkey_int8_512.onnx");
}

std::vector<ModelCatalogEntry> model_catalog() {
    return {
        make_model_entry("int8", 512, "corridorkey_int8_512.onnx", "onnx", "cpu",
                         "Validated macOS default for preview and 8 GB machines.",
                         "portable_preview", true, true, {"macos_apple_silicon"},
                         {"macos_apple_silicon"}, {"apple_silicon_8gb"}),
        make_model_entry("int8", 768, "corridorkey_int8_768.onnx", "onnx", "cpu",
                         "Validated CPU compatibility baseline for 16 GB Apple Silicon systems.",
                         "portable_default", true, false, {"macos_apple_silicon"},
                         {"macos_apple_silicon"}, {"apple_silicon_16gb"}),
        make_model_entry("int8", 1024, "corridorkey_int8_1024.onnx", "onnx", "cpu",
                         "Available for manual testing on high-memory systems.",
                         "manual_high_resolution_validation", false, false, {},
                         {"macos_apple_silicon"}, {}),
        make_model_entry("mlx", 2048, "corridorkey_mlx.safetensors", "safetensors", "mlx",
                         "Official Apple Silicon model pack for the first native MLX backend.",
                         "apple_acceleration_primary", true, true, {"macos_apple_silicon"},
                         {"macos_apple_silicon"}, {"apple_silicon_16gb"}),
        make_model_entry("fp16", 512, "corridorkey_fp16_512.onnx", "onnx", "tensorrt",
                         "GPU-focused reference variant for non-macOS expansion.",
                         "windows_rtx_reference", false, false, {}, {"windows_rtx"}, {}),
        make_model_entry("fp16", 768, "corridorkey_fp16_768.onnx", "onnx", "tensorrt",
                         "Higher quality GPU-focused reference variant.", "windows_rtx_reference",
                         false, false, {}, {"windows_rtx"}, {}),
        make_model_entry("fp16", 1024, "corridorkey_fp16_1024.onnx", "onnx", "tensorrt",
                         "Maximum GPU-focused reference variant.", "windows_rtx_reference", false,
                         false, {}, {"windows_rtx"}, {}),
        make_model_entry("fp32", 512, "corridorkey_fp32_512.onnx", "onnx", "cpu",
                         "Reference validation variant.", "reference_validation", false, false, {},
                         {"macos_apple_silicon", "windows_rtx"}, {}),
        make_model_entry("fp32", 768, "corridorkey_fp32_768.onnx", "onnx", "cpu",
                         "Higher resolution reference validation variant.", "reference_validation",
                         false, false, {}, {"macos_apple_silicon", "windows_rtx"}, {}),
        make_model_entry("fp32", 1024, "corridorkey_fp32_1024.onnx", "onnx", "cpu",
                         "Maximum resolution reference validation variant.", "reference_validation",
                         false, false, {}, {"macos_apple_silicon", "windows_rtx"}, {}),
    };
}

std::vector<PresetDefinition> preset_catalog() {
    return {
        PresetDefinition{
            "mac-preview",
            "Mac Preview",
            "Fast validation preset for smoke tests and low-memory systems.",
            InferenceParams{512, 1.0F, false, 400, 1.0F, false, 1, false, 32},
            "corridorkey_int8_512.onnx",
            "smoke_preview",
            false,
            {"macos_apple_silicon"},
            {"macos_apple_silicon"},
            {"apple_silicon_8gb"},
        },
        PresetDefinition{
            "mac-balanced",
            "Mac Balanced",
            "Default Apple Silicon preset using the native MLX model pack with automatic tiling "
            "and no implicit cleanup.",
            InferenceParams{0, 1.0F, false, 400, 1.0F, false, 1, true, 32},
            "corridorkey_mlx.safetensors",
            "apple_acceleration_primary",
            true,
            {"macos_apple_silicon"},
            {"macos_apple_silicon"},
            {"apple_silicon_16gb"},
        },
        PresetDefinition{
            "mac-max-quality",
            "Mac Max Quality",
            "Apple Silicon preset for higher-quality tiled runs with cleanup enabled.",
            InferenceParams{0, 1.0F, true, 400, 1.0F, false, 1, true, 64},
            "corridorkey_mlx.safetensors",
            "native_resolution_examples",
            false,
            {"macos_apple_silicon"},
            {"macos_apple_silicon"},
            {"apple_silicon_16gb_plus"},
        },
    };
}

}  // namespace corridorkey

namespace corridorkey::app {

std::optional<ModelCatalogEntry> find_model_by_filename(const std::string& filename) {
    return corridorkey::find_model_by_filename(filename);
}

std::optional<PresetDefinition> find_preset_by_selector(const std::string& selector) {
    return corridorkey::find_preset_by_selector(selector);
}

std::optional<PresetDefinition> default_preset_for_capabilities(
    const RuntimeCapabilities& capabilities) {
    return corridorkey::default_preset_for_capabilities(capabilities);
}

std::optional<ModelCatalogEntry> default_model_for_request(
    const RuntimeCapabilities& capabilities, Backend requested_backend,
    const std::optional<PresetDefinition>& preset) {
    return corridorkey::default_model_for_request(capabilities, requested_backend, preset);
}

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
        case Backend::MLX:
            return "mlx";
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
    json["mlx_probe_available"] = capabilities.mlx_probe_available;
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

nlohmann::json to_json(const StageTiming& timing) {
    nlohmann::json json;
    json["name"] = timing.name;
    json["total_ms"] = timing.total_ms;
    json["sample_count"] = timing.sample_count;
    json["work_units"] = timing.work_units;
    json["avg_ms"] = timing.sample_count > 0 ? timing.total_ms / timing.sample_count : 0.0;
    if (timing.work_units > 0) {
        json["ms_per_unit"] = timing.total_ms / timing.work_units;
    }
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
    if (!event.timings.empty()) {
        nlohmann::json timings = nlohmann::json::array();
        for (const auto& timing : event.timings) {
            timings.push_back(to_json(timing));
        }
        json["timings"] = timings;
    }
    return json;
}

nlohmann::json to_json(const ModelCatalogEntry& model) {
    nlohmann::json json;
    json["variant"] = model.variant;
    json["resolution"] = model.resolution;
    json["filename"] = model.filename;
    json["artifact_family"] = model.artifact_family;
    json["recommended_backend"] = model.recommended_backend;
    json["description"] = model.description;
    json["download_url"] = model.download_url;
    json["intended_use"] = model.intended_use;
    json["validated_for_macos"] = model.validated_for_macos;
    json["packaged_for_macos"] = model.packaged_for_macos;
    json["validated_platforms"] = model.validated_platforms;
    json["intended_platforms"] = model.intended_platforms;
    json["validated_hardware_tiers"] = model.validated_hardware_tiers;
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
    json["intended_use"] = preset.intended_use;
    json["default_for_macos"] = preset.default_for_macos;
    json["validated_platforms"] = preset.validated_platforms;
    json["intended_platforms"] = preset.intended_platforms;
    json["validated_hardware_tiers"] = preset.validated_hardware_tiers;
    json["params"] = params;
    return json;
}

}  // namespace corridorkey::app
