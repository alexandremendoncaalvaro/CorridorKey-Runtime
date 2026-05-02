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

// runtime_capabilities() is declared in include/corridorkey/engine.hpp and
// defined in runtime_contracts_probes.cpp (corridorkey_core); the wrappers
// in the corridorkey::app namespace below dispatch to it by qualified name.
// find_preset_by_selector lives in runtime_contracts.hpp (already included
// at the top of this file).

namespace {

// Resolution rungs the model catalog ships across all officially supported
// product tracks. Centralising them as constexpr removes the magic-number
// drift that accumulated when the catalog grew (cppcoreguidelines-avoid-magic-numbers).
constexpr int kRes512 = 512;
constexpr int kRes768 = 768;
constexpr int kRes1024 = 1024;
constexpr int kRes1536 = 1536;
constexpr int kRes2048 = 2048;

// VRAM tier breakpoints (MiB). Match the validation_tiers_for_device labels
// (rtx_8gb, rtx_10gb_plus, rtx_16gb_plus, rtx_24gb) and the resolution
// ceilings tables. Any tier change must move both the value and the matching
// validated_hardware_tiers metadata in the catalog entries below.
constexpr std::int64_t kVram8GbMiB = 8000;
constexpr std::int64_t kVram10GbMiB = 10000;
constexpr std::int64_t kVram16GbMiB = 16000;
constexpr std::int64_t kVram24GbMiB = 24000;

// Synthetic input dimensions used by the bench / probe paths in this TU.
constexpr int kProbeFrameDim = 64;

// Default despeckle window size when no caller-provided value is set.
// Chosen empirically — keep stable so the user-visible default remains
// reproducible across runs.
constexpr int kDefaultDespeckleSize = 400;

// Default tile padding for preset-built InferenceParams. Matches the
// padding the OFX render path uses when seam-blending tiled outputs.
constexpr int kDefaultTilePadding = 64;

// Sentinel target_resolution meaning "let the runtime pick based on the
// active artifact's recommended resolution". Used by the macOS presets
// where the MLX bridge is fixed at its compile resolution.
constexpr int kTargetResolutionAuto = 0;

bool has_backend(const RuntimeCapabilities& capabilities, Backend backend) {
    return std::ranges::find(capabilities.supported_backends, backend) !=
           capabilities.supported_backends.end();
}

std::string normalized_lower(std::string value) {
    std::ranges::transform(value, value.begin(),
                           [](unsigned char character) {
                               return static_cast<char>(std::tolower(character));
                           });
    return value;
}

std::optional<std::string> normalize_preset_selector(const std::string& selector) {
    if (selector.empty()) {
        return std::nullopt;
    }

    return normalized_lower(selector);
}

std::string normalize_packaged_model_profile_name(const std::string& value) {
    auto normalized = normalized_lower(value);
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
            if (parsed.contains("model_profile") && parsed.at("model_profile").is_string()) {
                return normalize_packaged_model_profile_name(
                    parsed.at("model_profile").get<std::string>());
            }
        } catch (...) {
            continue;
        }
    }

    return std::nullopt;
}

std::vector<std::string> windows_rtx_validation_tiers(std::int64_t available_memory_mb) {
    std::vector<std::string> tiers;
    if (available_memory_mb >= kVram8GbMiB) {
        tiers.emplace_back("rtx_8gb");
    }
    if (available_memory_mb >= kVram10GbMiB) {
        tiers.emplace_back("rtx_10gb");
        tiers.emplace_back("rtx_10gb_plus");
    }
    if (available_memory_mb >= kVram16GbMiB) {
        tiers.emplace_back("rtx_16gb_plus");
    }
    if (available_memory_mb >= kVram24GbMiB) {
        tiers.emplace_back("rtx_24gb");
    }
    return tiers;
}

std::vector<std::string> apple_silicon_validation_tiers(std::int64_t available_memory_mb) {
    std::vector<std::string> tiers;
    if (available_memory_mb >= kVram8GbMiB) {
        tiers.emplace_back("apple_silicon_8gb");
    }
    if (available_memory_mb >= kVram16GbMiB) {
        tiers.emplace_back("apple_silicon_16gb");
    }
    return tiers;
}

bool is_windows_rtx_device(const DeviceInfo& device, const RuntimeCapabilities& capabilities) {
    return capabilities.platform == "windows" &&
           (device.backend == Backend::TensorRT || device.backend == Backend::CUDA);
}

bool is_apple_silicon_device(const DeviceInfo& device, const RuntimeCapabilities& capabilities) {
    return capabilities.platform == "macos" &&
           (device.backend == Backend::MLX || device.backend == Backend::CoreML ||
            device.backend == Backend::Auto);
}

std::vector<std::string> validation_tiers_for_device(const DeviceInfo& device,
                                                     const RuntimeCapabilities& capabilities) {
    if (is_windows_rtx_device(device, capabilities)) {
        return windows_rtx_validation_tiers(device.available_memory_mb);
    }
    if (is_apple_silicon_device(device, capabilities)) {
        return apple_silicon_validation_tiers(device.available_memory_mb);
    }
    return {};
}

bool has_validated_tier_for_device(const ModelCatalogEntry& model, const DeviceInfo& device,
                                   const RuntimeCapabilities& capabilities) {
    auto device_tiers = validation_tiers_for_device(device, capabilities);
    return std::ranges::any_of(model.validated_hardware_tiers,
                       [&](const std::string& tier) {
                           return std::ranges::find(device_tiers, tier) !=
                                  device_tiers.end();
                       });
}

bool windows_tensorrt_packaged_resolution_supported(int resolution) {
    switch (resolution) {
        case kRes512:
        case kRes1024:
        case kRes1536:
        case kRes2048:
            return true;
        default:
            return false;
    }
}

std::optional<int> windows_tensorrt_resolution_ceiling(std::int64_t available_memory_mb) {
    if (available_memory_mb <= 0) {
        return std::nullopt;
    }
    if (available_memory_mb >= kVram24GbMiB) {
        return kRes2048;
    }
    if (available_memory_mb >= kVram16GbMiB) {
        return kRes1536;
    }
    if (available_memory_mb >= kVram10GbMiB) {
        return kRes1024;
    }
    return kRes512;
}

std::optional<int> windows_universal_resolution_ceiling(std::int64_t available_memory_mb) {
    if (available_memory_mb <= 0) {
        return std::nullopt;
    }
    if (available_memory_mb >= kVram10GbMiB) {
        return kRes1024;
    }
    return kRes512;
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
            .code = ErrorCode::InvalidParameters,
            .message =
                "Coarse-to-fine requested a smaller coarse artifact, but no valid packaged coarse "
                "resolution could be resolved for this request.",
        }};
    }

    return std::pair<int, bool>{*coarse_resolution, true};
}

ModelCatalogEntry make_model_entry(
    const std::string& variant, int resolution, const std::string& filename,
    const std::string& artifact_family, const std::string& recommended_backend,
    const std::string& description, const std::string& intended_use, bool validated_for_macos,
    bool packaged_for_macos, bool packaged_for_windows,
    std::vector<std::string> validated_platforms, std::vector<std::string> intended_platforms,
    std::vector<std::string> validated_hardware_tiers, std::string screen_color = "green") {
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
    params.despeckle_size = kDefaultDespeckleSize;
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

namespace {

// Bundles the blue / green filename pair for a single resolution tier.
// Wrapping the two const char* into a named struct removes the
// bugprone-easily-swappable-parameters risk (otherwise blue and green could
// be reversed at the call site without the compiler noticing) and reads
// naturally at the table sites below.
struct BlueGreenFilenames {
    const char* blue;
    const char* green;
};

// Picks the blue artifact when requested and packaged, otherwise falls back
// to the green rung. Encapsulates the "blue requested but not packaged ->
// green canonicalization fallback" rule the OFX render path detects via
// the returned entry's screen_color.
std::optional<ModelCatalogEntry> pick_blue_or_green(std::string_view screen_color,
                                                    BlueGreenFilenames filenames) {
    if (screen_color == "blue") {
        if (auto blue = app::find_model_by_filename(filenames.blue);
            blue.has_value() && blue->packaged_for_windows) {
            return blue;
        }
        // Fall through to green rung; OFX render path canonicalises blue input.
    }
    return app::find_model_by_filename(filenames.green);
}

// Resolves the Windows RTX (TensorRT or CUDA) catalog entry for the given
// memory tier. Sprint 1 PR 4: blue ships as TorchTRT .ts; blue 1536+ uses
// FP32 because Sprint 0 found FP16 NaNs at those graph sizes (HANDOFF
// section 2.2). Green stays on ONNX -> TRT-RTX EP.
std::optional<ModelCatalogEntry> windows_rtx_catalog_entry_for_tier(
    std::int64_t available_memory_mb, std::string_view screen_color) {
    if (available_memory_mb >= kVram24GbMiB) {
        return pick_blue_or_green(screen_color,
                                  BlueGreenFilenames{.blue = "corridorkey_blue_torchtrt_fp32_2048.ts",
                                                     .green = "corridorkey_fp16_2048.onnx"});
    }
    if (available_memory_mb >= kVram16GbMiB) {
        return pick_blue_or_green(screen_color,
                                  BlueGreenFilenames{.blue = "corridorkey_blue_torchtrt_fp32_1536.ts",
                                                     .green = "corridorkey_fp16_1536.onnx"});
    }
    if (available_memory_mb >= kVram10GbMiB) {
        return pick_blue_or_green(screen_color,
                                  BlueGreenFilenames{.blue = "corridorkey_blue_torchtrt_fp16_1024.ts",
                                                     .green = "corridorkey_fp16_1024.onnx"});
    }
    return pick_blue_or_green(screen_color,
                              BlueGreenFilenames{.blue = "corridorkey_blue_torchtrt_fp16_512.ts",
                                                 .green = "corridorkey_fp16_512.onnx"});
}

// Picks a Windows RTX entry, prioritising the preset's recommended model when
// it is validated for the device and the request is not blue (preset models
// are green-track; blue requests bypass preset and use the dedicated table).
std::optional<ModelCatalogEntry> windows_rtx_model_for_request(
    const RuntimeCapabilities& capabilities, const DeviceInfo& requested_device,
    const std::optional<PresetDefinition>& preset, std::string_view screen_color) {
    const bool prefer_blue = (screen_color == "blue");
    if (preset.has_value() && !prefer_blue) {
        auto preset_model = app::find_model_by_filename(preset->recommended_model);
        if (preset_model.has_value() &&
            has_validated_tier_for_device(*preset_model, requested_device, capabilities)) {
            return preset_model;
        }
    }
    return windows_rtx_catalog_entry_for_tier(requested_device.available_memory_mb, screen_color);
}

std::optional<ModelCatalogEntry> windows_universal_model_for_request(
    const DeviceInfo& requested_device) {
    if (requested_device.available_memory_mb >= kVram10GbMiB) {
        return app::find_model_by_filename("corridorkey_fp16_1024.onnx");
    }
    return app::find_model_by_filename("corridorkey_fp16_512.onnx");
}

std::optional<ModelCatalogEntry> apple_silicon_model_for_request(
    const std::optional<PresetDefinition>& preset) {
    if (preset.has_value()) {
        if (auto preset_model = app::find_model_by_filename(preset->recommended_model);
            preset_model.has_value()) {
            return preset_model;
        }
    }
    return app::find_model_by_filename("corridorkey_mlx.safetensors");
}

bool is_apple_silicon_request(const DeviceInfo& requested_device,
                              const RuntimeCapabilities& capabilities) {
    return (requested_device.backend == Backend::Auto ||
            requested_device.backend == Backend::MLX) &&
           capabilities.platform == "macos" && capabilities.apple_silicon;
}

bool is_windows_rtx_request(const DeviceInfo& requested_device,
                            const RuntimeCapabilities& capabilities) {
    if (capabilities.platform != "windows") return false;
    if (requested_device.backend == Backend::Auto ||
        requested_device.backend == Backend::TensorRT) {
        return has_backend(capabilities, Backend::TensorRT);
    }
    if (requested_device.backend == Backend::CUDA) {
        return has_backend(capabilities, Backend::CUDA);
    }
    return false;
}

bool is_windows_universal_request(const DeviceInfo& requested_device,
                                  const RuntimeCapabilities& capabilities) {
    return capabilities.platform == "windows" &&
           (requested_device.backend == Backend::DirectML ||
            requested_device.backend == Backend::WindowsML ||
            requested_device.backend == Backend::OpenVINO);
}

}  // namespace

std::optional<ModelCatalogEntry> default_model_for_request(
    const RuntimeCapabilities& capabilities, const DeviceInfo& requested_device,
    const std::optional<PresetDefinition>& preset, std::string_view screen_color) {
    if (requested_device.backend == Backend::CPU) {
        // CPU rendering retired together with INT8: the only ONNX artifact
        // packaged for CPU was corridorkey_int8_*, which carried unacceptable
        // quality loss. Callers that still ask for Backend::CPU must handle
        // the empty result rather than receive a downgraded fallback.
        return std::nullopt;
    }
    if (is_apple_silicon_request(requested_device, capabilities)) {
        return apple_silicon_model_for_request(preset);
    }
    if (is_windows_rtx_request(requested_device, capabilities)) {
        return windows_rtx_model_for_request(capabilities, requested_device, preset, screen_color);
    }
    if (is_windows_universal_request(requested_device, capabilities)) {
        return windows_universal_model_for_request(requested_device);
    }
    return std::nullopt;
}

std::vector<ModelCatalogEntry> model_catalog() {
    return {
        make_model_entry("mlx", kRes2048, "corridorkey_mlx.safetensors", "safetensors", "mlx",
                         "Official Apple Silicon model pack for the first native MLX backend.",
                         "apple_acceleration_primary", true, true, false, {"macos_apple_silicon"},
                         {"macos_apple_silicon"}, {"apple_silicon_16gb"}),
        make_model_entry("fp16", kRes512, "corridorkey_fp16_512.onnx", "onnx", "tensorrt",
                         "Lower-memory Windows RTX reference variant for lab bring-up.",
                         "windows_rtx_reference", false, false, true, {}, {"windows_rtx_30_plus"},
                         {"rtx_8gb"}),
        make_model_entry("fp16", kRes768, "corridorkey_fp16_768.onnx", "onnx", "tensorrt",
                         "Reference-only Windows RTX artifact retained for 768px investigation.",
                         "reference_validation", false, false, false, {}, {"windows_rtx_30_plus"},
                         {}),
        make_model_entry("fp16", kRes1024, "corridorkey_fp16_1024.onnx", "onnx", "tensorrt",
                         "Maximum quality Windows RTX pack for 10 GB and higher tiers.",
                         "windows_rtx_primary", false, false, true, {}, {"windows_rtx_30_plus"},
                         {"rtx_10gb_plus"}),
        make_model_entry("fp16", kRes1536, "corridorkey_fp16_1536.onnx", "onnx", "tensorrt",
                         "High-fidelity Windows RTX pack for 16 GB VRAM systems.",
                         "windows_rtx_primary", false, false, true, {}, {"windows_rtx_30_plus"},
                         {"rtx_16gb_plus"}),
        make_model_entry("fp16", kRes2048, "corridorkey_fp16_2048.onnx", "onnx", "tensorrt",
                         "Extreme quality Windows RTX pack for 24 GB VRAM systems.",
                         "windows_rtx_primary", false, false, true, {}, {"windows_rtx_30_plus"},
                         {"rtx_24gb"}),
        // Strategy C, Sprint 1 PR 4: blue Windows RTX is delivered via the
        // Torch-TensorRT runtime (HANDOFF.md "Sprint 0 outcome" + Sprint 1
        // plan). The ONNX -> TRT-RTX EP path NaN'd at >=1024 for blue;
        // TorchTRT loads cleanly at every resolution. The runtime DLL set
        // ships INSIDE the blue model pack rather than the base bundle, so
        // green users do not pay the ~5 GB cost.
        make_model_entry("fp16-blue", kRes512, "corridorkey_blue_torchtrt_fp16_512.ts", "torchtrt",
                         "torchtrt",
                         "Windows RTX blue-screen pack (Torch-TensorRT) for 8 GB tiers.",
                         "windows_rtx_blue_screen", false, false, true, {}, {"windows_rtx_30_plus"},
                         {"rtx_8gb"}, "blue"),
        make_model_entry(
            "fp16-blue", kRes1024, "corridorkey_blue_torchtrt_fp16_1024.ts", "torchtrt", "torchtrt",
            "Windows RTX blue-screen pack (Torch-TensorRT) for 10 GB and higher tiers.",
            "windows_rtx_blue_screen", false, false, true, {}, {"windows_rtx_30_plus"},
            {"rtx_10gb_plus"}, "blue"),
        // Blue 1536 ships as FP32 because Sprint 0 found FP16 NaNs at this
        // graph size for the blue checkpoint (HANDOFF.md section 2.2).
        // Blue 2048 follows the same precision constraint and is staged via
        // cloud GPU compile per HANDOFF Sprint 2.
        make_model_entry(
            "fp32-blue", kRes1536, "corridorkey_blue_torchtrt_fp32_1536.ts", "torchtrt", "torchtrt",
            "Windows RTX blue-screen pack (Torch-TensorRT, FP32) for 16 GB VRAM systems.",
            "windows_rtx_blue_screen", false, false, true, {}, {"windows_rtx_30_plus"},
            {"rtx_16gb_plus"}, "blue"),
        make_model_entry(
            "fp32-blue", kRes2048, "corridorkey_blue_torchtrt_fp32_2048.ts", "torchtrt", "torchtrt",
            "Windows RTX blue-screen pack (Torch-TensorRT, FP32) for 24 GB VRAM systems.",
            "windows_rtx_blue_screen", false, false, true, {}, {"windows_rtx_30_plus"},
            {"rtx_24gb"}, "blue"),
        make_model_entry("fp32", kRes512, "corridorkey_fp32_512.onnx", "onnx", "cpu",
                         "Reference validation variant.", "reference_validation", false, false,
                         false, {}, {"macos_apple_silicon", "windows_rtx"}, {}),
        make_model_entry("fp32", kRes768, "corridorkey_fp32_768.onnx", "onnx", "cpu",
                         "Higher resolution reference validation variant.", "reference_validation",
                         false, false, false, {}, {"macos_apple_silicon", "windows_rtx"}, {}),
        make_model_entry("fp32", kRes1024, "corridorkey_fp32_1024.onnx", "onnx", "cpu",
                         "High resolution reference validation variant.", "reference_validation",
                         false, false, false, {}, {"macos_apple_silicon", "windows_rtx"}, {}),
        make_model_entry("fp32", kRes1536, "corridorkey_fp32_1536.onnx", "onnx", "cpu",
                         "Ultra resolution variant for near-native 1080p inference.",
                         "reference_validation", false, false, false, {},
                         {"macos_apple_silicon", "windows_rtx"}, {}),
        make_model_entry("fp32", kRes2048, "corridorkey_fp32_2048.onnx", "onnx", "cpu",
                         "Maximum resolution variant matching Python reference pipeline.",
                         "reference_validation", false, false, false, {},
                         {"macos_apple_silicon", "windows_rtx"}, {}),
    };
}

std::vector<PresetDefinition> preset_catalog() {
    return {
        PresetDefinition{
            .id = "mac-balanced",
            .name = "Mac Balanced",
            .description =
                "Default Apple Silicon preset using the native MLX model pack with automatic "
                "tiling and no implicit cleanup.",
            .params =
                make_preset_inference_params(kTargetResolutionAuto, false, true, kDefaultTilePadding),
            .recommended_model = "corridorkey_mlx.safetensors",
            .intended_use = "apple_acceleration_primary",
            .default_for_macos = true,
            .default_for_windows = false,
            .validated_platforms = {"macos_apple_silicon"},
            .intended_platforms = {"macos_apple_silicon"},
            .validated_hardware_tiers = {"apple_silicon_16gb"},
        },
        PresetDefinition{
            .id = "mac-max-quality",
            .name = "Mac Max Quality",
            .description = "Apple Silicon preset for higher-quality tiled runs with cleanup enabled.",
            .params =
                make_preset_inference_params(kTargetResolutionAuto, true, true, kDefaultTilePadding),
            .recommended_model = "corridorkey_mlx.safetensors",
            .intended_use = "native_resolution_examples",
            .default_for_macos = false,
            .default_for_windows = false,
            .validated_platforms = {"macos_apple_silicon"},
            .intended_platforms = {"macos_apple_silicon"},
            .validated_hardware_tiers = {"apple_silicon_16gb_plus"},
        },
        PresetDefinition{
            .id = "win-rtx-balanced",
            .name = "Windows RTX Balanced",
            .description = "Default Windows RTX preset with FP16 inference, runtime cache enabled, "
                           "and tiling ready for portable bundles.",
            .params = make_preset_inference_params(kRes1024, false, true, kDefaultTilePadding),
            .recommended_model = "corridorkey_fp16_1024.onnx",
            .intended_use = "windows_rtx_primary",
            .default_for_macos = false,
            .default_for_windows = true,
            .validated_platforms = {"windows_rtx_30_plus"},
            .intended_platforms = {"windows_rtx_30_plus"},
            .validated_hardware_tiers = {"rtx_10gb_plus"},
        },
        PresetDefinition{
            .id = "win-rtx-max-quality",
            .name = "Windows RTX Max Quality",
            .description =
                "Higher-quality Windows RTX preset with cleanup enabled for the 10 GB and up tier.",
            .params = make_preset_inference_params(kRes1024, true, true, kDefaultTilePadding),
            .recommended_model = "corridorkey_fp16_1024.onnx",
            .intended_use = "windows_rtx_primary",
            .default_for_macos = false,
            .default_for_windows = false,
            .validated_platforms = {"windows_rtx_30_plus"},
            .intended_platforms = {"windows_rtx_30_plus"},
            .validated_hardware_tiers = {"rtx_10gb_plus"},
        },
        PresetDefinition{
            .id = "win-rtx-ultra-quality",
            .name = "Windows RTX Ultra Quality",
            .description =
                "Extreme quality Windows RTX preset with cleanup enabled for 24 GB VRAM systems.",
            .params = make_preset_inference_params(kRes2048, true, true, kDefaultTilePadding),
            .recommended_model = "corridorkey_fp16_2048.onnx",
            .intended_use = "windows_rtx_primary",
            .default_for_macos = false,
            .default_for_windows = false,
            .validated_platforms = {"windows_rtx_30_plus"},
            .intended_platforms = {"windows_rtx_30_plus"},
            .validated_hardware_tiers = {"rtx_24gb"},
        },
        PresetDefinition{
            .id = "mac-ultra-quality",
            .name = "Mac Ultra Quality",
            .description =
                "Extreme quality Apple Silicon preset using 2048px MLX bridge with cleanup enabled.",
            .params = make_preset_inference_params(kRes2048, true, true, kDefaultTilePadding),
            .recommended_model = "corridorkey_mlx.safetensors",
            .intended_use = "native_resolution_examples",
            .default_for_macos = false,
            .default_for_windows = false,
            .validated_platforms = {"macos_apple_silicon"},
            .intended_platforms = {"macos_apple_silicon"},
            .validated_hardware_tiers = {"apple_silicon_16gb_plus"},
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
        auto found = std::ranges::find_if(
            devices, [&](const DeviceInfo& device) { return device.backend == backend; });
        if (found == devices.end()) {
            return std::nullopt;
        }
        return *found;
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

    auto non_cpu = std::ranges::find_if(devices, [](const DeviceInfo& device) {
        return device.backend != Backend::CPU;
    });
    if (non_cpu != devices.end()) {
        return *non_cpu;
    }

    return devices.front();
}

namespace {

// Per-profile factories. Splitting the long if/else chain in
// runtime_optimization_profile_for_device into named builders keeps each
// rung self-contained and lets the dispatcher stay under the
// readability-function-size threshold.
RuntimeOptimizationProfile windows_rtx_profile() {
    return RuntimeOptimizationProfile{
        .id = "windows-rtx",
        .label = "Windows RTX",
        .intended_track = "windows_rtx",
        .backend_intent = "tensorrt",
        .fallback_policy = "safe_auto_quality_with_manual_override",
        .warmup_policy = "precompiled_context_or_first_run_compile",
        .certification_tier = "packaged_fp16_ladder_through_2048",
        .unrestricted_quality_attempt = true,
    };
}

RuntimeOptimizationProfile linux_rtx_cuda_profile() {
    return RuntimeOptimizationProfile{
        .id = "linux-rtx-cuda",
        .label = "Linux RTX (CUDA Execution Provider)",
        .intended_track = "linux_rtx",
        .backend_intent = "cuda",
        .fallback_policy = "experimental_gpu_then_cpu_tolerant_workflows",
        .warmup_policy = "provider_specific_session_warmup",
        .certification_tier = "experimental",
        .unrestricted_quality_attempt = false,
    };
}

RuntimeOptimizationProfile windows_directml_profile(const DeviceInfo& device) {
    return RuntimeOptimizationProfile{
        .id = "windows-directml",
        .label = "Windows DirectML",
        .intended_track = "windows_universal",
        .backend_intent = backend_to_string(device.backend),
        .fallback_policy = "experimental_gpu_then_cpu_tolerant_workflows",
        .warmup_policy = "provider_specific_session_warmup",
        .certification_tier = "experimental",
        .unrestricted_quality_attempt = false,
    };
}

RuntimeOptimizationProfile apple_silicon_mlx_profile() {
    return RuntimeOptimizationProfile{
        .id = "apple-silicon-mlx",
        .label = "Apple Silicon MLX",
        .intended_track = "apple_silicon",
        .backend_intent = "mlx",
        .fallback_policy = "curated_primary_pack_with_bridge_exports",
        .warmup_policy = "bridge_import_and_callable_compile",
        .certification_tier = "official_apple_silicon_track",
        .unrestricted_quality_attempt = true,
    };
}

RuntimeOptimizationProfile cpu_fallback_profile() {
    return RuntimeOptimizationProfile{
        .id = "cpu-fallback",
        .label = "CPU Fallback",
        .intended_track = "portable_fallback",
        .backend_intent = "cpu",
        .fallback_policy = "tolerant_workflow_only",
        .warmup_policy = "minimal",
        .certification_tier = "best_effort",
        .unrestricted_quality_attempt = false,
    };
}

bool is_linux_rtx_cuda(const DeviceInfo& device, const RuntimeCapabilities& capabilities) {
    return capabilities.platform == "linux" &&
           (device.backend == Backend::CUDA || device.backend == Backend::TensorRT);
}

bool is_windows_universal(const DeviceInfo& device, const RuntimeCapabilities& capabilities) {
    return capabilities.platform == "windows" &&
           (device.backend == Backend::DirectML || device.backend == Backend::WindowsML ||
            device.backend == Backend::OpenVINO);
}

}  // namespace

RuntimeOptimizationProfile runtime_optimization_profile_for_device(
    const RuntimeCapabilities& capabilities, const DeviceInfo& device) {
    if (is_windows_rtx_device(device, capabilities)) {
        return windows_rtx_profile();
    }
    if (is_linux_rtx_cuda(device, capabilities)) {
        return linux_rtx_cuda_profile();
    }
    if (is_windows_universal(device, capabilities)) {
        return windows_directml_profile(device);
    }
    if (is_apple_silicon_device(device, capabilities)) {
        return apple_silicon_mlx_profile();
    }
    return cpu_fallback_profile();
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
            std::ranges::any_of(model.validated_hardware_tiers,
                                [](const std::string& tier) { return tier.starts_with("rtx_"); }) ||
            std::ranges::any_of(model.validated_platforms, [](const std::string& platform) {
                return platform.starts_with("windows");
            });
        state.certified_for_active_device =
            state.certified_for_active_track &&
            has_validated_tier_for_device(model, device, capabilities);
    } else if (capabilities.platform == "macos") {
        state.certified_for_active_track =
            model.validated_for_macos ||
            std::ranges::find(model.validated_platforms, "macos_apple_silicon") !=
                model.validated_platforms.end();
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

// nlohmann::json's operator[] on an object auto-inserts; it does not have
// the throw-on-missing semantic of std::vector::operator[] that the
// cppcoreguidelines-pro-bounds-avoid-unchecked-container-access check
// targets. Wrap the to_json/from_json blocks with NOLINTBEGIN/NOLINTEND
// to silence the false positives for the standard JSON-build idiom; per-
// line NOLINTNEXTLINE would dwarf the actual code.
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
nlohmann::json to_json(const Error& error) {
    nlohmann::json json;
    json["code"] = static_cast<int>(error.code);
    json["message"] = error.message;
    return json;
}

std::optional<int> max_supported_resolution_for_device(const DeviceInfo& requested_device) {
    switch (requested_device.backend) {
        case Backend::CPU:
            return kRes512;
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
            if (resolution >= kRes2048) {
                return static_cast<int>(kVram24GbMiB);
            }
            if (resolution >= kRes1536) {
                return static_cast<int>(kVram16GbMiB);
            }
            if (resolution >= kRes1024) {
                return static_cast<int>(kVram10GbMiB);
            }
            return std::nullopt;
        case Backend::DirectML:
        case Backend::WindowsML:
        case Backend::OpenVINO:
            if (resolution >= kRes1024) {
                return static_cast<int>(kVram10GbMiB);
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
    for (char digit : token) {
        if (digit < '0' || digit > '9') {
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
    return filename.starts_with("corridorkey_") &&
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
        .code = ErrorCode::InvalidParameters,
        .message =
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
            .code = ErrorCode::InvalidParameters,
            .message = "Requested quality resolution must be greater than zero.",
        }};
    }
    if (coarse_resolution_override > 0 && coarse_resolution_override >= requested_resolution) {
        return Unexpected<Error>{Error{
            .code = ErrorCode::InvalidParameters,
            .message = "Coarse-to-fine requires --coarse-resolution to be smaller than the requested "
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
    constexpr std::array<int, 4> kFallbackResolutions = {kRes2048, kRes1536, kRes1024, kRes512};
    for (int resolution : kFallbackResolutions) {
        if (resolution > search_resolution) {
            continue;
        }
        if (require_exact_resolution && resolution != search_resolution) {
            continue;
        }

        auto artifact_paths =
            candidate_artifact_paths_for_request(models_root, requested_device.backend, resolution);
        expected.insert(expected.end(), artifact_paths.begin(), artifact_paths.end());
    }

    return expected;
}

namespace {

// Bundles the per-call state shared by the resolution-walk helpers below.
// Pointer members instead of references keep the struct trivially copyable
// (cppcoreguidelines-avoid-const-or-ref-data-members) while preserving the
// "borrowed, not owned" contract: every callsite constructs the struct on
// the stack with the active models_root / requested_device.
struct CandidateSelectionContext {
    const std::filesystem::path* models_root;
    const DeviceInfo* requested_device;
    int requested_resolution;
    int search_resolution;
    bool coarse_to_fine;
    bool require_exact_resolution;
};

// Appends selections for one resolution rung to the running vector.
// Returns true if at least one packaged artifact was found at that
// resolution. The bool lets the caller short-circuit when require_exact
// is set and the exact-resolution rung is empty.
bool append_selections_for_resolution(int resolution, const CandidateSelectionContext& context,
                                      std::vector<ArtifactSelection>& selections) {
    auto artifact_paths = candidate_artifact_paths_for_request(
        *context.models_root, context.requested_device->backend, resolution);
    bool found = false;
    for (const auto& artifact_path : artifact_paths) {
        if (!std::filesystem::exists(artifact_path)) {
            continue;
        }
        found = true;
        selections.push_back(ArtifactSelection{
            .executable_model_path = artifact_path,
            .requested_resolution = context.requested_resolution,
            .effective_resolution = resolution,
            .used_fallback =
                resolution != context.requested_resolution || context.coarse_to_fine,
            .coarse_to_fine = context.coarse_to_fine,
        });
    }
    return found;
}

bool should_skip_resolution(int resolution, const CandidateSelectionContext& context,
                            bool exact_artifact_available) {
    if (resolution > context.search_resolution) {
        return true;
    }
    return context.require_exact_resolution && resolution != context.search_resolution &&
           !exact_artifact_available;
}

}  // namespace

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

    const CandidateSelectionContext context{
        .models_root = &models_root,
        .requested_device = &requested_device,
        .requested_resolution = requested_resolution,
        .search_resolution = resolution_search->first,
        .coarse_to_fine = resolution_search->second,
        .require_exact_resolution =
            !allow_lower_resolution_fallback &&
            (!resolution_search->second || coarse_resolution_override > 0),
    };

    std::vector<ArtifactSelection> selections;
    bool exact_artifact_available = false;
    constexpr std::array<int, 4> kFallbackResolutions = {kRes2048, kRes1536, kRes1024, kRes512};
    for (int resolution : kFallbackResolutions) {
        if (should_skip_resolution(resolution, context, exact_artifact_available)) {
            continue;
        }
        const bool found = append_selections_for_resolution(resolution, context, selections);
        if (context.require_exact_resolution && resolution == context.search_resolution) {
            if (!found) {
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
    json["avg_ms"] = timing.sample_count > 0
                         ? timing.total_ms / static_cast<double>(timing.sample_count)
                         : 0.0;
    if (timing.work_units > 0) {
        json["ms_per_unit"] = timing.total_ms / static_cast<double>(timing.work_units);
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
// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

}  // namespace corridorkey::app
