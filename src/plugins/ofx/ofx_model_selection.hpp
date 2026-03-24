#pragma once

#include <algorithm>
#include <cctype>
#include <corridorkey/engine.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "app/runtime_contracts.hpp"
#include "ofx_constants.hpp"

namespace corridorkey::ofx {

struct BootstrapEngineCandidate {
    DeviceInfo device = {};
    std::filesystem::path requested_model_path = {};
    std::filesystem::path executable_model_path = {};
    int requested_resolution = 0;
    int effective_resolution = 0;
};

struct QualityArtifactSelection {
    std::filesystem::path executable_model_path = {};
    int requested_resolution = 0;
    int effective_resolution = 0;
    bool used_fallback = false;
};

inline const char* quality_mode_label(int quality_mode) {
    switch (quality_mode) {
        case kQualityPreview:
            return "Draft (512)";
        case kQualityStandard:
            return "Standard (768)";
        case kQualityHigh:
            return "High (1024)";
        case kQualityUltra:
            return "Ultra (1536)";
        case kQualityMaximum:
            return "Maximum (2048)";
        default:
            return "Auto";
    }
}

inline bool is_fixed_quality_mode(int quality_mode) {
    return quality_mode != kQualityAuto;
}

inline int clamp_quality_mode_for_cpu_backend(Backend backend, int quality_mode) {
    if (backend == Backend::CPU) {
        return kQualityPreview;
    }
    return quality_mode;
}

inline bool should_prepare_bootstrap_during_instance_create(bool use_runtime_server) {
    return !use_runtime_server;
}

inline int resolve_target_resolution(int quality_mode, int input_width, int input_height) {
    if (quality_mode == kQualityPreview) return 512;
    if (quality_mode == kQualityStandard) return 768;
    if (quality_mode == kQualityHigh) return 1024;
    if (quality_mode == kQualityUltra) return 1536;
    if (quality_mode == kQualityMaximum) return 2048;

    int max_dim = std::max(input_width, input_height);
    if (max_dim > 3000) return 2048;
    if (max_dim > 2000) return 1536;
    if (max_dim > 1000) return 1024;
    return 768;
}

inline int initial_requested_resolution_for_quality_mode(int quality_mode) {
    if (!is_fixed_quality_mode(quality_mode)) {
        return 0;
    }
    return resolve_target_resolution(quality_mode, 0, 0);
}

inline std::filesystem::path mlx_pack_path(const std::filesystem::path& models_root) {
    return models_root / "corridorkey_mlx.safetensors";
}

inline std::optional<int> resolution_from_model_path(const std::filesystem::path& path) {
    std::string stem = path.stem().string();
    std::size_t last_separator = stem.find_last_of('_');
    if (last_separator == std::string::npos || last_separator + 1 >= stem.size()) {
        return std::nullopt;
    }

    std::string token = stem.substr(last_separator + 1);
    if (token.empty() || !std::all_of(token.begin(), token.end(),
                                      [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
        return std::nullopt;
    }

    return std::stoi(token);
}

inline std::filesystem::path artifact_path_for_backend(const std::filesystem::path& models_root,
                                                       Backend backend, int resolution) {
    if (backend == Backend::MLX) {
        return models_root / ("corridorkey_mlx_bridge_" + std::to_string(resolution) + ".mlxfn");
    }
    if (backend == Backend::TensorRT || backend == Backend::CUDA) {
        return models_root / ("corridorkey_fp16_" + std::to_string(resolution) + ".onnx");
    }
    return models_root / ("corridorkey_int8_" + std::to_string(resolution) + ".onnx");
}

inline std::vector<std::filesystem::path> artifact_paths_for_backend(
    const std::filesystem::path& models_root, Backend backend, int resolution,
    int quantization_mode) {
    if (backend == Backend::MLX) {
        return {artifact_path_for_backend(models_root, backend, resolution)};
    }
    std::filesystem::path fp16_path =
        models_root / ("corridorkey_fp16_" + std::to_string(resolution) + ".onnx");
    std::filesystem::path int8_path =
        models_root / ("corridorkey_int8_" + std::to_string(resolution) + ".onnx");

    if (backend == Backend::TensorRT) {
        return {fp16_path};
    }
    if (backend == Backend::CUDA) {
        switch (quantization_mode) {
            case kQuantizationFp16:
                return {fp16_path, int8_path};
            case kQuantizationInt8:
                return {int8_path, fp16_path};
            default:  // kQuantizationAuto
                return {int8_path, fp16_path};
        }
    }

    switch (quantization_mode) {
        case kQuantizationFp16:
            return {fp16_path, int8_path};
        case kQuantizationInt8:
            return {int8_path, fp16_path};
        default:
            return {int8_path, fp16_path};
    }
}

inline bool path_exists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::exists(path, error) && !error;
}

inline bool has_mlx_bootstrap_artifacts(const std::filesystem::path& models_root) {
    return path_exists(mlx_pack_path(models_root)) &&
           path_exists(artifact_path_for_backend(models_root, Backend::MLX, 512));
}

inline std::vector<QualityArtifactSelection> quality_artifact_candidates(
    const std::filesystem::path& models_root, Backend backend, int quality_mode, int input_width,
    int input_height, int quantization_mode) {
    std::vector<QualityArtifactSelection> candidates;
    int requested_resolution = resolve_target_resolution(quality_mode, input_width, input_height);

    if (backend == Backend::DirectML && requested_resolution > 1536) {
        requested_resolution = 1536;
    }

    constexpr int kFallbackResolutions[] = {2048, 1536, 1024, 768, 512};
    for (int resolution : kFallbackResolutions) {
        if (resolution > requested_resolution) {
            continue;
        }
        // Fixed quality modes only consider the exact resolution. If the artifact is missing,
        // return an empty list so the caller can report a clear error. Fallback on engine
        // compilation failure is handled by ensure_engine_for_quality, not here.
        // Auto mode iterates all lower resolutions to find the best available artifact.
        if (is_fixed_quality_mode(quality_mode) && resolution != requested_resolution) {
            break;
        }

        auto artifact_paths =
            artifact_paths_for_backend(models_root, backend, resolution, quantization_mode);
        for (const auto& artifact_path : artifact_paths) {
            if (!path_exists(artifact_path)) {
                continue;
            }
            candidates.push_back(QualityArtifactSelection{artifact_path, requested_resolution,
                                                          resolution,
                                                          resolution != requested_resolution});
        }
    }

    return candidates;
}

inline std::vector<BootstrapEngineCandidate> build_bootstrap_candidates(
    const RuntimeCapabilities& capabilities, const DeviceInfo& detected_device,
    const std::filesystem::path& models_root) {
    std::vector<BootstrapEngineCandidate> candidates;

    auto append_unique = [&](BootstrapEngineCandidate candidate) {
        if (candidate.executable_model_path.empty()) {
            return;
        }
        auto duplicate = std::find_if(
            candidates.begin(), candidates.end(), [&](const BootstrapEngineCandidate& existing) {
                return existing.device.backend == candidate.device.backend &&
                       existing.executable_model_path == candidate.executable_model_path;
            });
        if (duplicate == candidates.end()) {
            candidates.push_back(std::move(candidate));
        }
    };

#if defined(__APPLE__)
    if (capabilities.platform == "macos" && capabilities.apple_silicon &&
        capabilities.mlx_probe_available && has_mlx_bootstrap_artifacts(models_root)) {
        append_unique(
            {DeviceInfo{"Apple Silicon MLX", detected_device.available_memory_mb, Backend::MLX},
             mlx_pack_path(models_root), artifact_path_for_backend(models_root, Backend::MLX, 512),
             512, 512});
    }
#else
    (void)capabilities;
#endif

    auto preset = app::default_preset_for_capabilities(capabilities);
    auto append_default_candidate = [&](const DeviceInfo& device) {
        auto model_entry = app::default_model_for_request(capabilities, device, preset);
        if (!model_entry.has_value()) {
            return;
        }

        auto requested_model_path = models_root / model_entry->filename;
        if (!path_exists(requested_model_path)) {
            return;
        }

        auto executable_model_path = requested_model_path;
        if (device.backend == Backend::MLX) {
            executable_model_path = artifact_path_for_backend(models_root, Backend::MLX, 512);
            if (!path_exists(executable_model_path)) {
                return;
            }
        }

        int effective_resolution = resolution_from_model_path(executable_model_path).value_or(512);
        int requested_resolution =
            resolution_from_model_path(requested_model_path).value_or(effective_resolution);
        append_unique({device, requested_model_path, executable_model_path, requested_resolution,
                       effective_resolution});
    };

    append_default_candidate(detected_device);
    if (detected_device.backend != Backend::CPU) {
        append_default_candidate(
            DeviceInfo{"Generic CPU", detected_device.available_memory_mb, Backend::CPU});
    }

    return candidates;
}

inline std::optional<QualityArtifactSelection> select_quality_artifact(
    const std::filesystem::path& models_root, Backend backend, int quality_mode, int input_width,
    int input_height, int quantization_mode) {
    auto candidates = quality_artifact_candidates(models_root, backend, quality_mode, input_width,
                                                  input_height, quantization_mode);
    if (!candidates.empty()) {
        return candidates.front();
    }

    return std::nullopt;
}

}  // namespace corridorkey::ofx
