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

bool has_backend(const RuntimeCapabilities& capabilities, Backend backend) {
    return std::find(capabilities.supported_backends.begin(), capabilities.supported_backends.end(),
                     backend) != capabilities.supported_backends.end();
}

std::string normalized_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::optional<std::string> normalize_preset_selector(const std::string& selector) {
    if (selector.empty()) {
        return std::nullopt;
    }

    return normalized_lower(selector);
}

std::string resolve_platform_preset_alias(const std::string& selector,
                                          const RuntimeCapabilities& capabilities) {
    if (selector == "preview") {
        if (capabilities.platform == "windows" && has_backend(capabilities, Backend::TensorRT)) {
            return "win-cpu-safe";
        }
        return "mac-preview";
    }
    if (selector == "balanced" || selector == "default") {
        if (capabilities.platform == "windows" && has_backend(capabilities, Backend::TensorRT)) {
            return "win-rtx-balanced";
        }
        return "mac-balanced";
    }
    if (selector == "max" || selector == "max-quality") {
        if (capabilities.platform == "windows" && has_backend(capabilities, Backend::TensorRT)) {
            return "win-rtx-max-quality";
        }
        return "mac-max-quality";
    }
    if (selector == "ultra" || selector == "maximum") {
        if (capabilities.platform == "windows" && has_backend(capabilities, Backend::TensorRT)) {
            return "win-rtx-ultra-quality";
        }
        return "mac-ultra-quality";
    }
    return selector;
}

ModelCatalogEntry make_model_entry(const std::string& variant, int resolution,
                                   const std::string& filename, const std::string& artifact_family,
                                   const std::string& recommended_backend,
                                   const std::string& description, const std::string& intended_use,
                                   bool validated_for_macos, bool packaged_for_macos,
                                   bool packaged_for_windows,
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
    entry.packaged_for_windows = packaged_for_windows;
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
    VideoFrameFormat input_format;
    auto video_support = inspect_video_output_support(input_format);
    capabilities.default_video_mode = video_support.default_mode;
    capabilities.default_video_container = video_support.default_container;
    capabilities.default_video_encoder = video_support.default_encoder;
    capabilities.lossless_video_available = video_support.lossless_available;
    capabilities.lossless_video_unavailable_reason = video_support.lossless_unavailable_reason;

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

    auto capabilities = runtime_capabilities();
    auto resolved_id = resolve_platform_preset_alias(*normalized, capabilities);

    for (const auto& preset : preset_catalog()) {
        if (normalized_lower(preset.id) == resolved_id) {
            return preset;
        }
    }

    return std::nullopt;
}

std::optional<PresetDefinition> default_preset_for_capabilities(
    const RuntimeCapabilities& capabilities) {
    if (capabilities.platform == "windows" && has_backend(capabilities, Backend::TensorRT)) {
        for (const auto& preset : preset_catalog()) {
            if (preset.default_for_windows) {
                return preset;
            }
        }
    }

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
    const RuntimeCapabilities& capabilities, const DeviceInfo& requested_device,
    const std::optional<PresetDefinition>& preset) {
    auto windows_rtx_model = [&]() -> std::optional<ModelCatalogEntry> {
        if (preset.has_value()) {
            auto preset_model = find_model_by_filename(preset->recommended_model);
            if (preset_model.has_value()) {
                return preset_model;
            }
        }

        if (requested_device.available_memory_mb >= 24000) {
            return find_model_by_filename("corridorkey_fp16_2048.onnx");
        }
        if (requested_device.available_memory_mb >= 16000) {
            return find_model_by_filename("corridorkey_fp16_1536.onnx");
        }
        if (requested_device.available_memory_mb >= 10000) {
            return find_model_by_filename("corridorkey_fp16_1024.onnx");
        }
        if (requested_device.available_memory_mb >= 8000) {
            return find_model_by_filename("corridorkey_fp16_768.onnx");
        }
        return find_model_by_filename("corridorkey_fp16_512.onnx");
    };

    auto windows_universal_model = [&]() -> std::optional<ModelCatalogEntry> {
        if (requested_device.available_memory_mb >= 10000) {
            return find_model_by_filename("corridorkey_int8_1024.onnx");
        }
        if (requested_device.available_memory_mb >= 8000) {
            return find_model_by_filename("corridorkey_int8_768.onnx");
        }
        return find_model_by_filename("corridorkey_int8_512.onnx");
    };

    if (requested_device.backend == Backend::CPU) {
        return find_model_by_filename("corridorkey_int8_512.onnx");
    }

    if ((requested_device.backend == Backend::Auto || requested_device.backend == Backend::MLX) &&
        capabilities.platform == "macos" && capabilities.apple_silicon) {
        if (preset.has_value()) {
            auto preset_model = find_model_by_filename(preset->recommended_model);
            if (preset_model.has_value()) {
                return preset_model;
            }
        }
        return find_model_by_filename("corridorkey_mlx.safetensors");
    }

    if (capabilities.platform == "windows" && has_backend(capabilities, Backend::TensorRT) &&
        (requested_device.backend == Backend::Auto ||
         requested_device.backend == Backend::TensorRT)) {
        return windows_rtx_model();
    }

    if (capabilities.platform == "windows" && requested_device.backend == Backend::CUDA &&
        has_backend(capabilities, Backend::CUDA)) {
        return windows_rtx_model();
    }

    if (capabilities.platform == "windows" && (requested_device.backend == Backend::DirectML ||
                                               requested_device.backend == Backend::WindowsML ||
                                               requested_device.backend == Backend::OpenVINO)) {
        return windows_universal_model();
    }

    return find_model_by_filename("corridorkey_int8_512.onnx");
}

std::vector<ModelCatalogEntry> model_catalog() {
    return {
        make_model_entry("int8", 512, "corridorkey_int8_512.onnx", "onnx", "cpu",
                         "Validated macOS default for preview and 8 GB machines.",
                         "portable_preview", true, true, true, {"macos_apple_silicon"},
                         {"macos_apple_silicon"}, {"apple_silicon_8gb"}),
        make_model_entry("int8", 768, "corridorkey_int8_768.onnx", "onnx", "cpu",
                         "Validated CPU compatibility baseline for 16 GB Apple Silicon systems.",
                         "portable_default", true, false, true, {"macos_apple_silicon"},
                         {"macos_apple_silicon"}, {"apple_silicon_16gb"}),
        make_model_entry("int8", 1024, "corridorkey_int8_1024.onnx", "onnx", "cpu",
                         "Available for manual testing on high-memory systems.",
                         "manual_high_resolution_validation", false, false, true, {},
                         {"macos_apple_silicon"}, {}),
        make_model_entry("mlx", 2048, "corridorkey_mlx.safetensors", "safetensors", "mlx",
                         "Official Apple Silicon model pack for the first native MLX backend.",
                         "apple_acceleration_primary", true, true, false, {"macos_apple_silicon"},
                         {"macos_apple_silicon"}, {"apple_silicon_16gb"}),
        make_model_entry("fp16", 512, "corridorkey_fp16_512.onnx", "onnx", "tensorrt",
                         "Lower-memory Windows RTX reference variant for lab bring-up.",
                         "windows_rtx_reference", false, false, true, {}, {"windows_rtx_30_plus"},
                         {"rtx_8gb"}),
        make_model_entry("fp16", 768, "corridorkey_fp16_768.onnx", "onnx", "tensorrt",
                         "Portable Windows RTX balanced pack for Ampere-class GPUs.",
                         "windows_rtx_primary", false, false, true, {}, {"windows_rtx_30_plus"},
                         {"rtx_8gb", "rtx_10gb"}),
        make_model_entry("fp16", 1024, "corridorkey_fp16_1024.onnx", "onnx", "tensorrt",
                         "Maximum quality Windows RTX pack for 10 GB and higher tiers.",
                         "windows_rtx_primary", false, false, true, {}, {"windows_rtx_30_plus"},
                         {"rtx_10gb_plus"}),
        make_model_entry("fp16", 1536, "corridorkey_fp16_1536.onnx", "onnx", "tensorrt",
                         "High-fidelity Windows RTX pack for 16 GB VRAM systems.",
                         "windows_rtx_primary", false, false, true, {}, {"windows_rtx_30_plus"},
                         {"rtx_16gb_plus"}),
        make_model_entry("fp16", 2048, "corridorkey_fp16_2048.onnx", "onnx", "tensorrt",
                         "Extreme quality Windows RTX pack for 24 GB VRAM systems.",
                         "windows_rtx_primary", false, false, true, {}, {"windows_rtx_30_plus"},
                         {"rtx_24gb"}),
        make_model_entry("fp32", 512, "corridorkey_fp32_512.onnx", "onnx", "cpu",
                         "Reference validation variant.", "reference_validation", false, false,
                         false, {}, {"macos_apple_silicon", "windows_rtx"}, {}),
        make_model_entry("fp32", 768, "corridorkey_fp32_768.onnx", "onnx", "cpu",
                         "Higher resolution reference validation variant.", "reference_validation",
                         false, false, false, {}, {"macos_apple_silicon", "windows_rtx"}, {}),
        make_model_entry("fp32", 1024, "corridorkey_fp32_1024.onnx", "onnx", "cpu",
                         "High resolution reference validation variant.", "reference_validation",
                         false, false, false, {}, {"macos_apple_silicon", "windows_rtx"}, {}),
        make_model_entry("fp32", 1536, "corridorkey_fp32_1536.onnx", "onnx", "cpu",
                         "Ultra resolution variant for near-native 1080p inference.",
                         "reference_validation", false, false, false, {},
                         {"macos_apple_silicon", "windows_rtx"}, {}),
        make_model_entry("fp32", 2048, "corridorkey_fp32_2048.onnx", "onnx", "cpu",
                         "Maximum resolution variant matching Python reference pipeline.",
                         "reference_validation", false, false, false, {},
                         {"macos_apple_silicon", "windows_rtx"}, {}),
    };
}

std::vector<PresetDefinition> preset_catalog() {
    return {
        PresetDefinition{
            "mac-preview",
            "Mac Preview",
            "Fast validation preset for smoke tests and low-memory systems.",
            InferenceParams{512, 1.0F, 0, false, 400, 1.0F, false, 1, false, 32},
            "corridorkey_int8_512.onnx",
            "smoke_preview",
            false,
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
            InferenceParams{0, 1.0F, 0, false, 400, 1.0F, false, 1, true, 64},
            "corridorkey_mlx.safetensors",
            "apple_acceleration_primary",
            true,
            false,
            {"macos_apple_silicon"},
            {"macos_apple_silicon"},
            {"apple_silicon_16gb"},
        },
        PresetDefinition{
            "mac-max-quality",
            "Mac Max Quality",
            "Apple Silicon preset for higher-quality tiled runs with cleanup enabled.",
            InferenceParams{0, 1.0F, 0, true, 400, 1.0F, false, 1, true, 64},
            "corridorkey_mlx.safetensors",
            "native_resolution_examples",
            false,
            false,
            {"macos_apple_silicon"},
            {"macos_apple_silicon"},
            {"apple_silicon_16gb_plus"},
        },
        PresetDefinition{
            "win-cpu-safe",
            "Windows CPU Safe",
            "Compatibility preset that keeps the Windows RTX bundle on the CPU fallback path.",
            InferenceParams{512, 1.0F, 0, false, 400, 1.0F, false, 1, false, 32},
            "corridorkey_int8_512.onnx",
            "windows_cpu_fallback",
            false,
            false,
            {"windows_rtx_30_plus"},
            {"windows_rtx_30_plus"},
            {"cpu_fallback"},
        },
        PresetDefinition{
            "win-rtx-balanced",
            "Windows RTX Balanced",
            "Default Windows RTX preset with FP16 inference, runtime cache enabled, and tiling "
            "ready for portable bundles.",
            InferenceParams{768, 1.0F, 0, false, 400, 1.0F, false, 1, true, 64},
            "corridorkey_fp16_768.onnx",
            "windows_rtx_primary",
            false,
            true,
            {"windows_rtx_30_plus"},
            {"windows_rtx_30_plus"},
            {"rtx_10gb_plus"},
        },
        PresetDefinition{
            "win-rtx-max-quality",
            "Windows RTX Max Quality",
            "Higher-quality Windows RTX preset with cleanup enabled for the 10 GB and up tier.",
            InferenceParams{1024, 1.0F, 0, true, 400, 1.0F, false, 1, true, 64},
            "corridorkey_fp16_1024.onnx",
            "windows_rtx_primary",
            false,
            false,
            {"windows_rtx_30_plus"},
            {"windows_rtx_30_plus"},
            {"rtx_10gb_plus"},
        },
        PresetDefinition{
            "win-rtx-ultra-quality",
            "Windows RTX Ultra Quality",
            "Extreme quality Windows RTX preset with cleanup enabled for 24 GB VRAM systems.",
            InferenceParams{2048, 1.0F, 0, true, 400, 1.0F, false, 1, true, 64},
            "corridorkey_fp16_2048.onnx",
            "windows_rtx_primary",
            false,
            false,
            {"windows_rtx_30_plus"},
            {"windows_rtx_30_plus"},
            {"rtx_24gb"},
        },
        PresetDefinition{
            "mac-ultra-quality",
            "Mac Ultra Quality",
            "Extreme quality Apple Silicon preset using 2048px MLX bridge with cleanup enabled.",
            InferenceParams{2048, 1.0F, 0, true, 400, 1.0F, false, 1, true, 64},
            "corridorkey_mlx.safetensors",
            "native_resolution_examples",
            false,
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
    const RuntimeCapabilities& capabilities, const DeviceInfo& requested_device,
    const std::optional<PresetDefinition>& preset) {
    return corridorkey::default_model_for_request(capabilities, requested_device, preset);
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

std::string video_output_mode_to_string(VideoOutputMode mode) {
    switch (mode) {
        case VideoOutputMode::Lossless:
            return "lossless";
        case VideoOutputMode::Balanced:
            return "balanced";
    }
    return "lossless";
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

std::optional<int> max_supported_resolution_for_device(const DeviceInfo& requested_device) {
    switch (requested_device.backend) {
        case Backend::CPU:
            return 512;
        case Backend::TensorRT:
        case Backend::CUDA:
            return windows_tensorrt_resolution_ceiling(requested_device.available_memory_mb);
        case Backend::DirectML:
        case Backend::WindowsML:
        case Backend::OpenVINO:
            return windows_universal_resolution_ceiling(requested_device.available_memory_mb);
        default:
            return std::nullopt;
    }
}

std::optional<int> minimum_supported_memory_mb_for_resolution(Backend backend, int resolution) {
    switch (backend) {
        case Backend::TensorRT:
        case Backend::CUDA:
            if (resolution >= 2048) {
                return 24000;
            }
            if (resolution >= 1536) {
                return 16000;
            }
            if (resolution >= 1024) {
                return 10000;
            }
            if (resolution >= 768) {
                return 8000;
            }
            return std::nullopt;
        case Backend::DirectML:
        case Backend::WindowsML:
        case Backend::OpenVINO:
            if (resolution >= 1024) {
                return 10000;
            }
            if (resolution >= 768) {
                return 8000;
            }
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

bool should_use_coarse_to_fine_for_request(const DeviceInfo& requested_device,
                                           int requested_resolution,
                                           QualityFallbackMode fallback_mode,
                                           int coarse_resolution_override) {
    if (fallback_mode == QualityFallbackMode::Direct) {
        return false;
    }
    if (fallback_mode == QualityFallbackMode::CoarseToFine) {
        return true;
    }
    if (coarse_resolution_override > 0 && coarse_resolution_override < requested_resolution) {
        return true;
    }
    auto max_resolution = max_supported_resolution_for_device(requested_device);
    if (!max_resolution.has_value()) {
        return false;
    }
    return requested_resolution > *max_resolution;
}

std::optional<int> coarse_artifact_resolution_for_request(const DeviceInfo& requested_device,
                                                          int requested_resolution,
                                                          int coarse_resolution_override) {
    if (coarse_resolution_override > 0) {
        if (coarse_resolution_override < requested_resolution) {
            return coarse_resolution_override;
        }
        return std::nullopt;
    }

    auto max_resolution = max_supported_resolution_for_device(requested_device);
    if (!max_resolution.has_value()) {
        return std::nullopt;
    }

    const int safe_resolution = std::min(*max_resolution, 1024);
    if (safe_resolution <= 0 || safe_resolution >= requested_resolution) {
        return std::nullopt;
    }
    return safe_resolution;
}
std::optional<int> packaged_model_resolution(const std::filesystem::path& model_path) {
    const std::string stem = model_path.stem().string();
    const std::size_t separator = stem.find_last_of('_');
    if (separator == std::string::npos || separator + 1 >= stem.size()) {
        return std::nullopt;
    }

    const std::string token = stem.substr(separator + 1);
    if (token.empty()) {
        return std::nullopt;
    }
    for (char ch : token) {
        if (ch < '0' || ch > '9') {
            return std::nullopt;
        }
    }

    return std::stoi(token);
}

bool is_packaged_corridorkey_model(const std::filesystem::path& model_path) {
    const std::string filename = model_path.filename().string();
    return filename.rfind("corridorkey_", 0) == 0 && packaged_model_resolution(model_path).has_value();
}

std::filesystem::path sibling_model_path_for_resolution(const std::filesystem::path& model_path,
                                                        int resolution) {
    if (!is_packaged_corridorkey_model(model_path)) {
        return {};
    }

    const auto current_resolution = packaged_model_resolution(model_path);
    if (!current_resolution.has_value()) {
        return {};
    }

    std::string filename = model_path.filename().string();
    const std::string current_token = "_" + std::to_string(*current_resolution);
    const std::size_t token_pos = filename.rfind(current_token);
    if (token_pos == std::string::npos) {
        return {};
    }

    filename.replace(token_pos + 1, current_token.size() - 1, std::to_string(resolution));
    return model_path.parent_path() / filename;
}

Result<void> validate_refinement_mode_for_artifact(const std::filesystem::path& model_path,
                                                   RefinementMode refinement_mode) {
    if (refinement_mode == RefinementMode::Auto) {
        return {};
    }

    return Unexpected<Error>{Error{
        ErrorCode::InvalidParameters,
        "The selected runtime artifact does not advertise a validated refinement strategy "
        "override. Use refinement mode 'auto' with the current packaged model family: " +
            model_path.filename().string(),
    }};
}

Result<std::filesystem::path> resolve_model_artifact_for_request(
    const std::filesystem::path& model_path, const InferenceParams& params,
    const DeviceInfo& requested_device) {
    const int model_resolution = packaged_model_resolution(model_path).value_or(0);
    const int requested_resolution =
        params.requested_quality_resolution > 0
            ? params.requested_quality_resolution
            : (params.target_resolution > 0 ? params.target_resolution : model_resolution);

    const bool has_override = params.coarse_resolution_override > 0;
    const bool coarse_to_fine_requested =
        params.quality_fallback_mode == QualityFallbackMode::CoarseToFine || has_override;
    if (coarse_to_fine_requested && has_override &&
        params.coarse_resolution_override >= requested_resolution) {
        return Unexpected<Error>{Error{
            ErrorCode::InvalidParameters,
            "Coarse-to-fine requires --coarse-resolution to be smaller than the requested "
            "quality.",
        }};
    }

    const auto validate_resolved_model = [&](const std::filesystem::path& resolved_model_path)
        -> Result<std::filesystem::path> {
        auto refinement_validation =
            validate_refinement_mode_for_artifact(resolved_model_path, params.refinement_mode);
        if (!refinement_validation) {
            return Unexpected<Error>(refinement_validation.error());
        }
        return resolved_model_path;
    };

    if (!should_use_coarse_to_fine_for_request(requested_device, requested_resolution,
                                               params.quality_fallback_mode,
                                               params.coarse_resolution_override)) {
        return validate_resolved_model(model_path);
    }

    if (!is_packaged_corridorkey_model(model_path)) {
        return Unexpected<Error>{Error{
            ErrorCode::InvalidParameters,
            "Explicit --model only supports coarse-to-fine for packaged CorridorKey artifacts. "
            "Use a packaged model or switch --quality-fallback to direct.",
        }};
    }

    auto coarse_resolution = coarse_artifact_resolution_for_request(
        requested_device, requested_resolution, params.coarse_resolution_override);
    if (!coarse_resolution.has_value() || *coarse_resolution >= requested_resolution) {
        return Unexpected<Error>{Error{
            ErrorCode::InvalidParameters,
            "Coarse-to-fine requested a smaller coarse artifact, but no valid packaged coarse "
            "resolution could be resolved for this request.",
        }};
    }

    auto coarse_model_path = sibling_model_path_for_resolution(model_path, *coarse_resolution);
    if (coarse_model_path.empty()) {
        return Unexpected<Error>{Error{
            ErrorCode::InvalidParameters,
            "Coarse-to-fine requested a smaller packaged artifact, but the artifact name "
            "could not be derived from the selected model.",
        }};
    }

    if (!std::filesystem::exists(coarse_model_path)) {
        return Unexpected<Error>{Error{
            ErrorCode::ModelLoadFailed,
            "Coarse-to-fine requested a packaged coarse artifact, but it is missing: " +
                coarse_model_path.string(),
        }};
    }

    return validate_resolved_model(coarse_model_path);
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
    json["default_video_mode"] = video_output_mode_to_string(capabilities.default_video_mode);
    json["default_video_container"] = capabilities.default_video_container;
    json["default_video_encoder"] = capabilities.default_video_encoder;
    json["lossless_video_available"] = capabilities.lossless_video_available;
    json["lossless_video_unavailable_reason"] = capabilities.lossless_video_unavailable_reason;

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
    json["packaged_for_windows"] = model.packaged_for_windows;
    json["validated_platforms"] = model.validated_platforms;
    json["intended_platforms"] = model.intended_platforms;
    json["validated_hardware_tiers"] = model.validated_hardware_tiers;
    return json;
}

nlohmann::json to_json(const PresetDefinition& preset) {
    nlohmann::json params;
    params["target_resolution"] = preset.params.target_resolution;
    params["despill_strength"] = preset.params.despill_strength;
    params["spill_method"] = preset.params.spill_method;
    params["auto_despeckle"] = preset.params.auto_despeckle;
    params["despeckle_size"] = preset.params.despeckle_size;
    params["refiner_scale"] = preset.params.refiner_scale;
    params["input_is_linear"] = preset.params.input_is_linear;
    params["batch_size"] = preset.params.batch_size;
    params["enable_tiling"] = preset.params.enable_tiling;
    params["tile_padding"] = preset.params.tile_padding;
    params["source_passthrough"] = preset.params.source_passthrough;
    params["sp_erode_px"] = preset.params.sp_erode_px;
    params["sp_blur_px"] = preset.params.sp_blur_px;
    params["requested_quality_resolution"] = preset.params.requested_quality_resolution;
    params["quality_fallback_mode"] =
        static_cast<int>(preset.params.quality_fallback_mode);
    params["refinement_mode"] = static_cast<int>(preset.params.refinement_mode);
    params["coarse_resolution_override"] = preset.params.coarse_resolution_override;

    nlohmann::json json;
    json["id"] = preset.id;
    json["name"] = preset.name;
    json["description"] = preset.description;
    json["recommended_model"] = preset.recommended_model;
    json["intended_use"] = preset.intended_use;
    json["default_for_macos"] = preset.default_for_macos;
    json["default_for_windows"] = preset.default_for_windows;
    json["validated_platforms"] = preset.validated_platforms;
    json["intended_platforms"] = preset.intended_platforms;
    json["validated_hardware_tiers"] = preset.validated_hardware_tiers;
    json["params"] = params;
    return json;
}

}  // namespace corridorkey::app
