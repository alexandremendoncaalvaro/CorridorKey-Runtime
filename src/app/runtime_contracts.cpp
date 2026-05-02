#include "runtime_contracts.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <utility>

#include "../common/runtime_paths.hpp"
#include "../core/mlx_probe.hpp"
#include "../frame_io/video_io.hpp"

namespace corridorkey {

// Forward declarations for the probe-using entry points whose definitions
// live in runtime_contracts_probes.cpp (corridorkey_core). The wrappers in
// the corridorkey::app namespace below dispatch to them by qualified name,
// and corridorkey_common cannot define them because they pull device-
// detection probes that link against ONNX Runtime.
RuntimeCapabilities runtime_capabilities();
std::optional<PresetDefinition> find_preset_by_selector(const std::string& selector);

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

std::string normalize_packaged_model_profile_name(const std::string& value) {
    const auto normalized = normalized_lower(value);
    if (normalized == "rtx-lite" || normalized == "rtx-stable" || normalized == "rtx-full" ||
        normalized == "windows-rtx") {
        return "windows-rtx";
    }
    if (normalized == "windows-universal") {
        return "windows-universal";
    }
    return normalized;
}

std::optional<std::string> detect_active_packaged_model_profile() {
    const auto models_root = common::default_models_root();
    if (models_root.empty() || models_root.filename() != "models") {
        return std::nullopt;
    }

    std::vector<std::filesystem::path> inventory_candidates = {
        models_root.parent_path() / "model_inventory.json",
    };

    const auto parent = models_root.parent_path();
    if (parent.filename() == "Resources" && parent.parent_path().filename() == "Contents") {
        inventory_candidates.push_back(parent.parent_path().parent_path() / "model_inventory.json");
    }

    for (const auto& inventory_path : inventory_candidates) {
        std::error_code error;
        if (!std::filesystem::exists(inventory_path, error) || error) {
            continue;
        }

        try {
            std::ifstream stream(inventory_path);
            if (!stream.is_open()) {
                continue;
            }

            nlohmann::json parsed = nlohmann::json::parse(stream, nullptr, true, true);
            if (parsed.contains("model_profile") && parsed["model_profile"].is_string()) {
                return normalize_packaged_model_profile_name(
                    parsed["model_profile"].get<std::string>());
            }
        } catch (...) {
            continue;
        }
    }

    return std::nullopt;
}

std::vector<std::string> validation_tiers_for_device(const DeviceInfo& device,
                                                     const RuntimeCapabilities& capabilities) {
    std::vector<std::string> tiers;

    if (capabilities.platform == "windows" &&
        (device.backend == Backend::TensorRT || device.backend == Backend::CUDA)) {
        if (device.available_memory_mb >= 8000) {
            tiers.push_back("rtx_8gb");
        }
        if (device.available_memory_mb >= 10000) {
            tiers.push_back("rtx_10gb");
            tiers.push_back("rtx_10gb_plus");
        }
        if (device.available_memory_mb >= 16000) {
            tiers.push_back("rtx_16gb_plus");
        }
        if (device.available_memory_mb >= 24000) {
            tiers.push_back("rtx_24gb");
        }
    }

    if (capabilities.platform == "macos" &&
        (device.backend == Backend::MLX || device.backend == Backend::CoreML ||
         device.backend == Backend::Auto)) {
        if (device.available_memory_mb >= 8000) {
            tiers.push_back("apple_silicon_8gb");
        }
        if (device.available_memory_mb >= 16000) {
            tiers.push_back("apple_silicon_16gb");
        }
    }

    return tiers;
}

bool has_validated_tier_for_device(const ModelCatalogEntry& model, const DeviceInfo& device,
                                   const RuntimeCapabilities& capabilities) {
    auto device_tiers = validation_tiers_for_device(device, capabilities);
    return std::any_of(model.validated_hardware_tiers.begin(), model.validated_hardware_tiers.end(),
                       [&](const std::string& tier) {
                           return std::find(device_tiers.begin(), device_tiers.end(), tier) !=
                                  device_tiers.end();
                       });
}

bool windows_tensorrt_packaged_resolution_supported(int resolution) {
    switch (resolution) {
        case 512:
        case 1024:
        case 1536:
        case 2048:
            return true;
        default:
            return false;
    }
}

std::optional<int> windows_tensorrt_resolution_ceiling(std::int64_t available_memory_mb) {
    if (available_memory_mb <= 0) {
        return std::nullopt;
    }
    if (available_memory_mb >= 24000) {
        return 2048;
    }
    if (available_memory_mb >= 16000) {
        return 1536;
    }
    if (available_memory_mb >= 10000) {
        return 1024;
    }
    return 512;
}

std::optional<int> windows_universal_resolution_ceiling(std::int64_t available_memory_mb) {
    if (available_memory_mb <= 0) {
        return std::nullopt;
    }
    if (available_memory_mb >= 10000) {
        return 1024;
    }
    return 512;
}

std::string resolve_platform_preset_alias(const std::string& selector,
                                          const RuntimeCapabilities& capabilities) {
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

std::optional<ModelCatalogEntry> model_catalog_entry_for_path(
    const std::filesystem::path& model_path) {
    return app::find_model_by_filename(model_path.filename().string());
}

std::vector<std::filesystem::path> candidate_artifact_paths_for_request(
    const std::filesystem::path& models_root, Backend backend, int resolution) {
    if (backend == Backend::MLX) {
        return {models_root / ("corridorkey_mlx_bridge_" + std::to_string(resolution) + ".mlxfn")};
    }

    if ((backend == Backend::TensorRT || backend == Backend::CUDA) &&
        !windows_tensorrt_packaged_resolution_supported(resolution)) {
        return {};
    }

    return {models_root / ("corridorkey_fp16_" + std::to_string(resolution) + ".onnx")};
}

Result<std::pair<int, bool>> search_resolution_for_request(
    const DeviceInfo& requested_device, int requested_resolution, QualityFallbackMode fallback_mode,
    int coarse_resolution_override, bool allow_unrestricted_quality_attempt) {
    const bool coarse_to_fine = app::should_use_coarse_to_fine_for_request(
        requested_device, requested_resolution, fallback_mode, coarse_resolution_override,
        allow_unrestricted_quality_attempt);
    if (!coarse_to_fine) {
        return std::pair<int, bool>{requested_resolution, false};
    }

    auto coarse_resolution = app::coarse_artifact_resolution_for_request(
        requested_device, requested_resolution, coarse_resolution_override);
    if (!coarse_resolution.has_value() || *coarse_resolution >= requested_resolution) {
        return Unexpected<Error>{Error{
            ErrorCode::InvalidParameters,
            "Coarse-to-fine requested a smaller coarse artifact, but no valid packaged coarse "
            "resolution could be resolved for this request.",
        }};
    }

    return std::pair<int, bool>{*coarse_resolution, true};
}

ModelCatalogEntry make_model_entry(const std::string& variant, int resolution,
                                   const std::string& filename, const std::string& artifact_family,
                                   const std::string& recommended_backend,
                                   const std::string& description, const std::string& intended_use,
                                   bool validated_for_macos, bool packaged_for_macos,
                                   bool packaged_for_windows,
                                   std::vector<std::string> validated_platforms,
                                   std::vector<std::string> intended_platforms,
                                   std::vector<std::string> validated_hardware_tiers,
                                   std::string screen_color = "green") {
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
    entry.screen_color = std::move(screen_color);
    return entry;
}

InferenceParams make_preset_inference_params(int target_resolution, bool auto_despeckle,
                                             bool enable_tiling, int tile_padding) {
    InferenceParams params;
    params.target_resolution = target_resolution;
    params.despill_strength = 1.0F;
    params.spill_method = 0;
    params.auto_despeckle = auto_despeckle;
    params.despeckle_size = 400;
    params.refiner_scale = 1.0F;
    params.alpha_hint_policy = AlphaHintPolicy::AutoRoughFallback;
    params.input_is_linear = false;
    params.batch_size = 1;
    params.enable_tiling = enable_tiling;
    params.tile_padding = tile_padding;
    return params;
}

}  // namespace

// runtime_capabilities() lives in runtime_contracts_probes.cpp (corridorkey_core)
// because it pulls device-detection probes that link against ONNX Runtime.
// The .ofx plugin links corridorkey_common only and never calls this function.

std::optional<ModelCatalogEntry> find_model_by_filename(const std::string& filename) {
    for (const auto& entry : model_catalog()) {
        if (entry.filename == filename) {
            return entry;
        }
    }

    return std::nullopt;
}

// find_preset_by_selector() lives in runtime_contracts_probes.cpp because it
// internally calls runtime_capabilities(). The .ofx never calls this entry
// point.

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
    const std::optional<PresetDefinition>& preset, std::string_view screen_color) {
    const bool prefer_blue = (screen_color == "blue");

    auto windows_rtx_model = [&]() -> std::optional<ModelCatalogEntry> {
        if (preset.has_value() && !prefer_blue) {
            auto preset_model = find_model_by_filename(preset->recommended_model);
            if (preset_model.has_value() &&
                has_validated_tier_for_device(*preset_model, requested_device, capabilities)) {
                return preset_model;
            }
        }

        const auto pick = [&](const char* blue_name,
                              const char* green_name) -> std::optional<ModelCatalogEntry> {
            if (prefer_blue) {
                if (auto blue = find_model_by_filename(blue_name);
                    blue.has_value() && blue->packaged_for_windows) {
                    return blue;
                }
                // Blue requested but the dedicated artifact is not in the
                // catalog (or not packaged for Windows): fall back to the
                // green rung. The OFX render path detects this by comparing
                // the returned entry's screen_color against the requested
                // "blue" and then runs the canonicalization workaround.
            }
            return find_model_by_filename(green_name);
        };

        if (requested_device.available_memory_mb >= 24000) {
            return pick("corridorkey_blue_fp16_2048.onnx", "corridorkey_fp16_2048.onnx");
        }
        if (requested_device.available_memory_mb >= 16000) {
            return pick("corridorkey_blue_fp16_1536.onnx", "corridorkey_fp16_1536.onnx");
        }
        if (requested_device.available_memory_mb >= 10000) {
            return pick("corridorkey_blue_fp16_1024.onnx", "corridorkey_fp16_1024.onnx");
        }
        return pick("corridorkey_blue_fp16_512.onnx", "corridorkey_fp16_512.onnx");
    };

    auto windows_universal_model = [&]() -> std::optional<ModelCatalogEntry> {
        if (requested_device.available_memory_mb >= 10000) {
            return find_model_by_filename("corridorkey_fp16_1024.onnx");
        }
        return find_model_by_filename("corridorkey_fp16_512.onnx");
    };

    if (requested_device.backend == Backend::CPU) {
        // CPU rendering retired together with INT8: the only ONNX artifact
        // packaged for CPU was corridorkey_int8_*, which carried unacceptable
        // quality loss. Callers that still ask for Backend::CPU must handle
        // the empty result rather than receive a downgraded fallback.
        return std::nullopt;
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

    return std::nullopt;
}

std::vector<ModelCatalogEntry> model_catalog() {
    return {
        make_model_entry("mlx", 2048, "corridorkey_mlx.safetensors", "safetensors", "mlx",
                         "Official Apple Silicon model pack for the first native MLX backend.",
                         "apple_acceleration_primary", true, true, false, {"macos_apple_silicon"},
                         {"macos_apple_silicon"}, {"apple_silicon_16gb"}),
        make_model_entry("fp16", 512, "corridorkey_fp16_512.onnx", "onnx", "tensorrt",
                         "Lower-memory Windows RTX reference variant for lab bring-up.",
                         "windows_rtx_reference", false, false, true, {}, {"windows_rtx_30_plus"},
                         {"rtx_8gb"}),
        make_model_entry("fp16", 768, "corridorkey_fp16_768.onnx", "onnx", "tensorrt",
                         "Reference-only Windows RTX artifact retained for 768px investigation.",
                         "reference_validation", false, false, false, {}, {"windows_rtx_30_plus"},
                         {}),
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
        // Strategy C, Sprint 1 PR 4: blue Windows RTX is delivered via the
        // Torch-TensorRT runtime (HANDOFF.md "Sprint 0 outcome" + Sprint 1
        // plan). The ONNX -> TRT-RTX EP path NaN'd at >=1024 for blue;
        // TorchTRT loads cleanly at every resolution. The runtime DLL set
        // ships INSIDE the blue model pack rather than the base bundle, so
        // green users do not pay the ~5 GB cost.
        make_model_entry("fp16-blue", 512, "corridorkey_blue_torchtrt_fp16_512.ts", "torchtrt",
                         "torchtrt",
                         "Windows RTX blue-screen pack (Torch-TensorRT) for 8 GB tiers.",
                         "windows_rtx_blue_screen", false, false, true, {}, {"windows_rtx_30_plus"},
                         {"rtx_8gb"}, "blue"),
        make_model_entry("fp16-blue", 1024, "corridorkey_blue_torchtrt_fp16_1024.ts", "torchtrt",
                         "torchtrt",
                         "Windows RTX blue-screen pack (Torch-TensorRT) for 10 GB and higher tiers.",
                         "windows_rtx_blue_screen", false, false, true, {}, {"windows_rtx_30_plus"},
                         {"rtx_10gb_plus"}, "blue"),
        // Blue 1536 ships as FP32 because Sprint 0 found FP16 NaNs at this
        // graph size for the blue checkpoint (HANDOFF.md section 2.2).
        // Blue 2048 follows the same precision constraint and is staged via
        // cloud GPU compile per HANDOFF Sprint 2.
        make_model_entry("fp32-blue", 1536, "corridorkey_blue_torchtrt_fp32_1536.ts", "torchtrt",
                         "torchtrt",
                         "Windows RTX blue-screen pack (Torch-TensorRT, FP32) for 16 GB VRAM systems.",
                         "windows_rtx_blue_screen", false, false, true, {}, {"windows_rtx_30_plus"},
                         {"rtx_16gb_plus"}, "blue"),
        make_model_entry("fp32-blue", 2048, "corridorkey_blue_torchtrt_fp32_2048.ts", "torchtrt",
                         "torchtrt",
                         "Windows RTX blue-screen pack (Torch-TensorRT, FP32) for 24 GB VRAM systems.",
                         "windows_rtx_blue_screen", false, false, true, {}, {"windows_rtx_30_plus"},
                         {"rtx_24gb"}, "blue"),
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
            "mac-balanced",
            "Mac Balanced",
            "Default Apple Silicon preset using the native MLX model pack with automatic tiling "
            "and no implicit cleanup.",
            make_preset_inference_params(0, false, true, 64),
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
            make_preset_inference_params(0, true, true, 64),
            "corridorkey_mlx.safetensors",
            "native_resolution_examples",
            false,
            false,
            {"macos_apple_silicon"},
            {"macos_apple_silicon"},
            {"apple_silicon_16gb_plus"},
        },
        PresetDefinition{
            "win-rtx-balanced",
            "Windows RTX Balanced",
            "Default Windows RTX preset with FP16 inference, runtime cache enabled, and tiling "
            "ready for portable bundles.",
            make_preset_inference_params(1024, false, true, 64),
            "corridorkey_fp16_1024.onnx",
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
            make_preset_inference_params(1024, true, true, 64),
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
            make_preset_inference_params(2048, true, true, 64),
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
            make_preset_inference_params(2048, true, true, 64),
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

std::optional<std::string> active_packaged_model_profile() {
    return detect_active_packaged_model_profile();
}

std::optional<DeviceInfo> preferred_runtime_device(const RuntimeCapabilities& capabilities,
                                                   const std::vector<DeviceInfo>& devices) {
    if (devices.empty()) {
        return std::nullopt;
    }

    auto prefer_backend = [&](Backend backend) -> std::optional<DeviceInfo> {
        auto it = std::find_if(devices.begin(), devices.end(),
                               [&](const DeviceInfo& device) { return device.backend == backend; });
        if (it == devices.end()) {
            return std::nullopt;
        }
        return *it;
    };

    if (capabilities.platform == "windows") {
        if (auto preferred = prefer_backend(Backend::TensorRT); preferred.has_value()) {
            return preferred;
        }
        if (auto preferred = prefer_backend(Backend::DirectML); preferred.has_value()) {
            return preferred;
        }
    }

    if (capabilities.platform == "macos") {
        if (auto preferred = prefer_backend(Backend::MLX); preferred.has_value()) {
            return preferred;
        }
        if (auto preferred = prefer_backend(Backend::CoreML); preferred.has_value()) {
            return preferred;
        }
    }

    auto non_cpu = std::find_if(devices.begin(), devices.end(), [](const DeviceInfo& device) {
        return device.backend != Backend::CPU;
    });
    if (non_cpu != devices.end()) {
        return *non_cpu;
    }

    return devices.front();
}

RuntimeOptimizationProfile runtime_optimization_profile_for_device(
    const RuntimeCapabilities& capabilities, const DeviceInfo& device) {
    RuntimeOptimizationProfile profile;

    if (capabilities.platform == "windows" &&
        (device.backend == Backend::TensorRT || device.backend == Backend::CUDA)) {
        profile.id = "windows-rtx";
        profile.label = "Windows RTX";
        profile.intended_track = "windows_rtx";
        profile.backend_intent = "tensorrt";
        profile.fallback_policy = "safe_auto_quality_with_manual_override";
        profile.warmup_policy = "precompiled_context_or_first_run_compile";
        profile.certification_tier = "packaged_fp16_ladder_through_2048";
        profile.unrestricted_quality_attempt = true;
        return profile;
    }

    if (capabilities.platform == "linux" &&
        (device.backend == Backend::CUDA || device.backend == Backend::TensorRT)) {
        profile.id = "linux-rtx-cuda";
        profile.label = "Linux RTX (CUDA Execution Provider)";
        profile.intended_track = "linux_rtx";
        profile.backend_intent = "cuda";
        profile.fallback_policy = "experimental_gpu_then_cpu_tolerant_workflows";
        profile.warmup_policy = "provider_specific_session_warmup";
        profile.certification_tier = "experimental";
        profile.unrestricted_quality_attempt = false;
        return profile;
    }

    if (capabilities.platform == "windows" &&
        (device.backend == Backend::DirectML || device.backend == Backend::WindowsML ||
         device.backend == Backend::OpenVINO)) {
        profile.id = "windows-directml";
        profile.label = "Windows DirectML";
        profile.intended_track = "windows_universal";
        profile.backend_intent = backend_to_string(device.backend);
        profile.fallback_policy = "experimental_gpu_then_cpu_tolerant_workflows";
        profile.warmup_policy = "provider_specific_session_warmup";
        profile.certification_tier = "experimental";
        profile.unrestricted_quality_attempt = false;
        return profile;
    }

    if (capabilities.platform == "macos" &&
        (device.backend == Backend::MLX || device.backend == Backend::CoreML ||
         device.backend == Backend::Auto)) {
        profile.id = "apple-silicon-mlx";
        profile.label = "Apple Silicon MLX";
        profile.intended_track = "apple_silicon";
        profile.backend_intent = "mlx";
        profile.fallback_policy = "curated_primary_pack_with_bridge_exports";
        profile.warmup_policy = "bridge_import_and_callable_compile";
        profile.certification_tier = "official_apple_silicon_track";
        profile.unrestricted_quality_attempt = true;
        return profile;
    }

    profile.id = "cpu-fallback";
    profile.label = "CPU Fallback";
    profile.intended_track = "portable_fallback";
    profile.backend_intent = "cpu";
    profile.fallback_policy = "tolerant_workflow_only";
    profile.warmup_policy = "minimal";
    profile.certification_tier = "best_effort";
    profile.unrestricted_quality_attempt = false;
    return profile;
}

ArtifactRuntimeState artifact_runtime_state_for_device(const ModelCatalogEntry& model,
                                                       const RuntimeCapabilities& capabilities,
                                                       const DeviceInfo& device, bool usable) {
    ArtifactRuntimeState state;
    state.present = usable;
    state.packaged_for_active_track =
        capabilities.platform == "windows" ? model.packaged_for_windows : model.packaged_for_macos;

    if (capabilities.platform == "windows") {
        state.certified_for_active_track =
            std::any_of(model.validated_hardware_tiers.begin(),
                        model.validated_hardware_tiers.end(),
                        [](const std::string& tier) { return tier.rfind("rtx_", 0) == 0; }) ||
            std::any_of(
                model.validated_platforms.begin(), model.validated_platforms.end(),
                [](const std::string& platform) { return platform.rfind("windows", 0) == 0; });
        state.certified_for_active_device =
            state.certified_for_active_track &&
            has_validated_tier_for_device(model, device, capabilities);
    } else if (capabilities.platform == "macos") {
        state.certified_for_active_track =
            model.validated_for_macos ||
            std::find(model.validated_platforms.begin(), model.validated_platforms.end(),
                      "macos_apple_silicon") != model.validated_platforms.end();
        state.certified_for_active_device =
            state.certified_for_active_track &&
            (model.validated_hardware_tiers.empty() ||
             has_validated_tier_for_device(model, device, capabilities));
    }

    auto recommended_model = corridorkey::app::default_model_for_request(
        capabilities, device, corridorkey::app::default_preset_for_capabilities(capabilities));
    state.recommended_for_active_device =
        usable && recommended_model.has_value() && recommended_model->filename == model.filename;
    state.certified_for_active_device = usable && state.certified_for_active_device;

    if (!state.packaged_for_active_track) {
        state.state = "reference_only";
    } else if (!state.present) {
        state.state = "missing";
    } else if (state.recommended_for_active_device) {
        state.state = "recommended";
    } else if (state.certified_for_active_device) {
        state.state = "certified";
    } else {
        state.state = "packaged";
    }

    return state;
}

std::optional<ModelCatalogEntry> find_model_by_filename(const std::string& filename) {
    return corridorkey::find_model_by_filename(filename);
}

// app::find_preset_by_selector() wrapper lives in runtime_contracts_probes.cpp
// because it dispatches to corridorkey::find_preset_by_selector, which is
// itself defined there (it depends on device-detection probes that bring
// ONNX Runtime into the link line).

std::optional<PresetDefinition> default_preset_for_capabilities(
    const RuntimeCapabilities& capabilities) {
    return corridorkey::default_preset_for_capabilities(capabilities);
}

std::optional<ModelCatalogEntry> default_model_for_request(
    const RuntimeCapabilities& capabilities, const DeviceInfo& requested_device,
    const std::optional<PresetDefinition>& preset, std::string_view screen_color) {
    return corridorkey::default_model_for_request(capabilities, requested_device, preset,
                                                  screen_color);
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
        case Backend::TorchTRT:
            return "torchtrt";
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
            return std::nullopt;
        case Backend::DirectML:
        case Backend::WindowsML:
        case Backend::OpenVINO:
            if (resolution >= 1024) {
                return 10000;
            }
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

bool should_use_coarse_to_fine_for_request(const DeviceInfo& requested_device,
                                           int requested_resolution,
                                           QualityFallbackMode fallback_mode,
                                           int coarse_resolution_override,
                                           bool allow_unrestricted_quality_attempt) {
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
    if (allow_unrestricted_quality_attempt && requested_resolution > *max_resolution) {
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
    if (auto catalog_entry = model_catalog_entry_for_path(model_path); catalog_entry.has_value()) {
        return catalog_entry->resolution;
    }

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
    if (auto catalog_entry = model_catalog_entry_for_path(model_path); catalog_entry.has_value()) {
        return true;
    }

    const std::string filename = model_path.filename().string();
    return filename.rfind("corridorkey_", 0) == 0 &&
           packaged_model_resolution(model_path).has_value();
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

Result<std::vector<std::filesystem::path>> expected_artifact_paths_for_request(
    const std::filesystem::path& models_root, const DeviceInfo& requested_device,
    int requested_resolution, bool allow_lower_resolution_fallback,
    QualityFallbackMode fallback_mode, int coarse_resolution_override,
    bool allow_unrestricted_quality_attempt) {
    if (requested_resolution <= 0) {
        return Unexpected<Error>{Error{
            ErrorCode::InvalidParameters,
            "Requested quality resolution must be greater than zero.",
        }};
    }
    if (coarse_resolution_override > 0 && coarse_resolution_override >= requested_resolution) {
        return Unexpected<Error>{Error{
            ErrorCode::InvalidParameters,
            "Coarse-to-fine requires --coarse-resolution to be smaller than the requested "
            "quality.",
        }};
    }

    auto resolution_search = search_resolution_for_request(
        requested_device, requested_resolution, fallback_mode, coarse_resolution_override,
        allow_unrestricted_quality_attempt);
    if (!resolution_search) {
        return Unexpected<Error>(resolution_search.error());
    }

    const int search_resolution = resolution_search->first;
    const bool coarse_to_fine = resolution_search->second;
    const bool require_exact_resolution =
        !allow_lower_resolution_fallback && (!coarse_to_fine || coarse_resolution_override > 0);

    std::vector<std::filesystem::path> expected;
    constexpr int kFallbackResolutions[] = {2048, 1536, 1024, 512};
    for (int resolution : kFallbackResolutions) {
        if (resolution > search_resolution) {
            continue;
        }
        if (require_exact_resolution && resolution != search_resolution) {
            continue;
        }

        auto artifact_paths = candidate_artifact_paths_for_request(
            models_root, requested_device.backend, resolution);
        expected.insert(expected.end(), artifact_paths.begin(), artifact_paths.end());
    }

    return expected;
}

Result<std::vector<ArtifactSelection>> quality_artifact_candidates_for_request(
    const std::filesystem::path& models_root, const DeviceInfo& requested_device,
    int requested_resolution, bool allow_lower_resolution_fallback,
    QualityFallbackMode fallback_mode, int coarse_resolution_override,
    bool allow_unrestricted_quality_attempt) {
    auto expected_paths = expected_artifact_paths_for_request(
        models_root, requested_device, requested_resolution, allow_lower_resolution_fallback,
        fallback_mode, coarse_resolution_override, allow_unrestricted_quality_attempt);
    if (!expected_paths) {
        return Unexpected<Error>(expected_paths.error());
    }

    auto resolution_search = search_resolution_for_request(
        requested_device, requested_resolution, fallback_mode, coarse_resolution_override,
        allow_unrestricted_quality_attempt);
    if (!resolution_search) {
        return Unexpected<Error>(resolution_search.error());
    }

    const int search_resolution = resolution_search->first;
    const bool coarse_to_fine = resolution_search->second;
    const bool require_exact_resolution =
        !allow_lower_resolution_fallback && (!coarse_to_fine || coarse_resolution_override > 0);

    std::vector<ArtifactSelection> selections;
    bool exact_artifact_available = false;
    constexpr int kFallbackResolutions[] = {2048, 1536, 1024, 512};
    for (int resolution : kFallbackResolutions) {
        if (resolution > search_resolution) {
            continue;
        }
        if (require_exact_resolution && resolution != search_resolution &&
            !exact_artifact_available) {
            continue;
        }

        auto artifact_paths = candidate_artifact_paths_for_request(
            models_root, requested_device.backend, resolution);
        bool found_for_resolution = false;
        for (const auto& artifact_path : artifact_paths) {
            if (!std::filesystem::exists(artifact_path)) {
                continue;
            }
            found_for_resolution = true;
            selections.push_back(ArtifactSelection{
                artifact_path,
                requested_resolution,
                resolution,
                resolution != requested_resolution || coarse_to_fine,
                coarse_to_fine,
            });
        }

        if (require_exact_resolution && resolution == search_resolution) {
            if (!found_for_resolution) {
                return std::vector<ArtifactSelection>{};
            }
            exact_artifact_available = true;
        }
    }

    return selections;
}

// resolve_model_artifact_for_request() lives in runtime_contracts_probes.cpp
// because it calls runtime_capabilities(). The .ofx never calls this entry
// point; it asks the runtime server to resolve quality artifacts.

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
    json["screen_color"] = model.screen_color;
    return json;
}

nlohmann::json to_json(const PresetDefinition& preset) {
    nlohmann::json params;
    params["target_resolution"] = preset.params.target_resolution;
    params["despill_strength"] = preset.params.despill_strength;
    params["spill_method"] = preset.params.spill_method;
    params["despill_screen_channel"] = preset.params.despill_screen_channel;
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
    params["quality_fallback_mode"] = static_cast<int>(preset.params.quality_fallback_mode);
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

nlohmann::json to_json(const RuntimeOptimizationProfile& profile) {
    nlohmann::json json;
    json["id"] = profile.id;
    json["label"] = profile.label;
    json["intended_track"] = profile.intended_track;
    json["backend_intent"] = profile.backend_intent;
    json["fallback_policy"] = profile.fallback_policy;
    json["warmup_policy"] = profile.warmup_policy;
    json["certification_tier"] = profile.certification_tier;
    json["unrestricted_quality_attempt"] = profile.unrestricted_quality_attempt;
    return json;
}

nlohmann::json to_json(const ArtifactRuntimeState& state) {
    nlohmann::json json;
    json["packaged_for_active_track"] = state.packaged_for_active_track;
    json["present"] = state.present;
    json["certified_for_active_track"] = state.certified_for_active_track;
    json["certified_for_active_device"] = state.certified_for_active_device;
    json["recommended_for_active_device"] = state.recommended_for_active_device;
    json["state"] = state.state;
    return json;
}

}  // namespace corridorkey::app
