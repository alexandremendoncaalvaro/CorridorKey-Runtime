#pragma once

#include <algorithm>
#include <cctype>
#include <corridorkey/engine.hpp>
#include <cstdint>
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
    bool coarse_to_fine = false;
};

struct QualityCompileFailureCacheContext {
    std::filesystem::path models_root = {};
    std::uint64_t models_bundle_token = 0;
    Backend backend = Backend::Auto;
    int device_index = 0;
    std::int64_t available_memory_mb = 0;
    int quantization_mode = kQuantizationFp16;
};

struct QualityCompileFailureEntry {
    std::filesystem::path artifact_path = {};
    int requested_resolution = 0;
    int effective_resolution = 0;
    std::string error_message = {};
};

struct QualityCompileFailureCache {
    QualityCompileFailureCacheContext context = {};
    std::vector<QualityCompileFailureEntry> entries = {};
    bool initialized = false;
};

struct CachedQualityCompileFailure {
    QualityArtifactSelection selection = {};
    std::string error_message = {};
};

inline const char* quality_mode_label(int quality_mode) {
    return quality_mode_ui_label(quality_mode);
}

inline std::string quality_fallback_warning(int quality_mode,
                                            const QualityArtifactSelection& selection) {
    if (!selection.used_fallback) {
        return {};
    }

    if (selection.coarse_to_fine) {
        return std::string(quality_mode_label(quality_mode)) + " (" +
               std::to_string(selection.requested_resolution) +
               "px) will run coarse-to-fine using the " +
               std::to_string(selection.effective_resolution) + "px packaged artifact";
    }

    return std::string(quality_mode_label(quality_mode)) + " (" +
           std::to_string(selection.requested_resolution) +
           "px) unavailable on this hardware -- using " +
           std::to_string(selection.effective_resolution) + "px";
}

inline bool is_fixed_quality_mode(int quality_mode) {
    return quality_mode != kQualityAuto;
}

inline bool use_quality_compile_failure_cache(Backend backend) {
    return backend == Backend::TensorRT;
}

inline bool quality_compile_failure_cache_matches(
    const QualityCompileFailureCache& cache,
    const QualityCompileFailureCacheContext& context) {
    return cache.initialized && cache.context.models_root == context.models_root &&
           cache.context.models_bundle_token == context.models_bundle_token &&
           cache.context.backend == context.backend &&
           cache.context.device_index == context.device_index &&
           cache.context.available_memory_mb == context.available_memory_mb &&
           cache.context.quantization_mode == context.quantization_mode;
}

inline void prepare_quality_compile_failure_cache(
    QualityCompileFailureCache& cache, const QualityCompileFailureCacheContext& context) {
    if (!quality_compile_failure_cache_matches(cache, context)) {
        cache.context = context;
        cache.entries.clear();
        cache.initialized = true;
    }
}

inline std::optional<CachedQualityCompileFailure> cached_quality_compile_failure(
    const QualityCompileFailureCache& cache,
    const QualityCompileFailureCacheContext& context,
    const QualityArtifactSelection& selection) {
    if (!use_quality_compile_failure_cache(context.backend) ||
        !quality_compile_failure_cache_matches(cache, context)) {
        return std::nullopt;
    }

    auto existing = std::find_if(
        cache.entries.begin(), cache.entries.end(),
        [&](const QualityCompileFailureEntry& entry) {
            return entry.artifact_path == selection.executable_model_path &&
                   entry.requested_resolution == selection.requested_resolution &&
                   entry.effective_resolution == selection.effective_resolution;
        });
    if (existing == cache.entries.end()) {
        return std::nullopt;
    }

    CachedQualityCompileFailure cached;
    cached.selection = selection;
    cached.error_message = existing->error_message;
    return cached;
}

inline void record_quality_compile_failure(QualityCompileFailureCache& cache,
                                           const QualityCompileFailureCacheContext& context,
                                           const QualityArtifactSelection& selection,
                                           const std::string& error_message) {
    if (!use_quality_compile_failure_cache(context.backend)) {
        return;
    }

    prepare_quality_compile_failure_cache(cache, context);
    auto existing = std::find_if(
        cache.entries.begin(), cache.entries.end(),
        [&](const QualityCompileFailureEntry& entry) {
            return entry.artifact_path == selection.executable_model_path &&
                   entry.requested_resolution == selection.requested_resolution &&
                   entry.effective_resolution == selection.effective_resolution;
        });
    if (existing != cache.entries.end()) {
        existing->error_message = error_message;
        return;
    }

    cache.entries.push_back(QualityCompileFailureEntry{
        selection.executable_model_path,
        selection.requested_resolution,
        selection.effective_resolution,
        error_message,
    });
}

inline std::vector<QualityArtifactSelection> filter_quality_artifacts_with_compile_cache(
    const std::vector<QualityArtifactSelection>& candidates,
    const QualityCompileFailureCache& cache,
    const QualityCompileFailureCacheContext& context) {
    if (!use_quality_compile_failure_cache(context.backend) ||
        !quality_compile_failure_cache_matches(cache, context)) {
        return candidates;
    }

    std::vector<QualityArtifactSelection> filtered;
    filtered.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        if (!cached_quality_compile_failure(cache, context, candidate).has_value()) {
            filtered.push_back(candidate);
        }
    }
    return filtered;
}

inline bool should_abort_quality_fallback_after_compile_failure(
    Backend backend, int quality_mode, bool cpu_quality_guardrail_active,
    const QualityArtifactSelection& selection) {
    return use_quality_compile_failure_cache(backend) && is_fixed_quality_mode(quality_mode) &&
           !cpu_quality_guardrail_active &&
           selection.effective_resolution == selection.requested_resolution;
}

inline std::optional<std::string> unsupported_quantization_message(Backend backend,
                                                                   int quantization_mode) {
    if (quantization_mode != kQuantizationInt8) {
        return std::nullopt;
    }

    if (backend == Backend::TensorRT) {
        return "INT8 (Compact) is not supported by the TensorRT RTX execution provider. "
               "Please use FP16 (Full).";
    }

    if (backend == Backend::DirectML) {
        return "INT8 (Compact) is not yet validated for the DirectML execution provider. "
               "Please use FP16 (Full) for AMD/DirectML runs.";
    }

    return std::nullopt;
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

inline std::string format_artifact_filename_list(
    const std::vector<std::filesystem::path>& artifact_paths) {
    std::string formatted;
    for (const auto& artifact_path : artifact_paths) {
        if (!formatted.empty()) {
            formatted += ", ";
        }
        formatted += artifact_path.filename().string();
    }
    return formatted;
}

inline std::optional<std::filesystem::path> primary_expected_artifact_path(
    const std::vector<std::filesystem::path>& artifact_paths) {
    if (artifact_paths.empty()) {
        return std::nullopt;
    }
    return artifact_paths.front();
}

inline std::vector<std::filesystem::path> expected_quality_artifact_paths(
    const std::filesystem::path& models_root, Backend backend, int quality_mode, int input_width,
    int input_height, int quantization_mode, std::int64_t available_memory_mb = 0,
    QualityFallbackMode fallback_mode = QualityFallbackMode::Auto,
    int coarse_resolution_override = 0) {
    const int effective_quality_mode = clamp_quality_mode_for_cpu_backend(backend, quality_mode);
    const int effective_requested_resolution =
        resolve_target_resolution(effective_quality_mode, input_width, input_height);
    DeviceInfo device{"", available_memory_mb, backend};
    int search_resolution = quality_search_resolution(device, effective_quality_mode,
                                                      effective_requested_resolution);

    if (app::should_use_coarse_to_fine_for_request(device, effective_requested_resolution,
                                                   fallback_mode, coarse_resolution_override)) {
        if (auto coarse_resolution = app::coarse_artifact_resolution_for_request(
                device, effective_requested_resolution, coarse_resolution_override);
            coarse_resolution.has_value()) {
            search_resolution = *coarse_resolution;
        }
    }

    if (backend == Backend::DirectML && search_resolution > 1536) {
        search_resolution = 1536;
    }

    std::vector<std::filesystem::path> expected;
    constexpr int kFallbackResolutions[] = {2048, 1536, 1024, 768, 512};
    const bool coarse_to_fine = app::should_use_coarse_to_fine_for_request(
        device, effective_requested_resolution, fallback_mode, coarse_resolution_override);
    const bool require_exact_resolution = is_fixed_quality_mode(effective_quality_mode) &&
                                          (!coarse_to_fine || coarse_resolution_override > 0);

    for (int resolution : kFallbackResolutions) {
        if (resolution > search_resolution) {
            continue;
        }
        if (require_exact_resolution && resolution != search_resolution) {
            continue;
        }
        auto artifact_paths =
            artifact_paths_for_backend(models_root, backend, resolution, quantization_mode);
        expected.insert(expected.end(), artifact_paths.begin(), artifact_paths.end());
    }

    return expected;
}

inline std::string missing_artifact_message(
    const std::string& prefix, const std::filesystem::path& models_root,
    const std::vector<std::filesystem::path>& expected_artifacts) {
    std::string message = prefix + " in " + models_root.string();
    if (expected_artifacts.empty()) {
        message += ".";
        return message;
    }

    message += expected_artifacts.size() == 1 ? ". Expected artifact: " : ". Expected one of: ";
    message += format_artifact_filename_list(expected_artifacts);
    message += ".";
    return message;
}

inline std::string missing_quality_artifact_message(
    const std::filesystem::path& models_root, Backend backend, int quality_mode, int input_width,
    int input_height, int quantization_mode, bool cpu_quality_guardrail_active,
    std::int64_t available_memory_mb = 0,
    QualityFallbackMode fallback_mode = QualityFallbackMode::Auto,
    int coarse_resolution_override = 0) {
    const auto expected_artifacts =
        expected_quality_artifact_paths(models_root, backend, quality_mode, input_width,
                                        input_height, quantization_mode, available_memory_mb,
                                        fallback_mode, coarse_resolution_override);
    if (cpu_quality_guardrail_active) {
        return missing_artifact_message(
            "CPU backend is limited to Draft (512), but the required model artifact is missing",
            models_root, expected_artifacts);
    }

    return missing_artifact_message(
        "Requested quality " + std::string(quality_mode_label(quality_mode)) +
            " is missing the required model artifact",
        models_root, expected_artifacts);
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
    int input_height, int quantization_mode, std::int64_t available_memory_mb = 0,
    QualityFallbackMode fallback_mode = QualityFallbackMode::Auto,
    int coarse_resolution_override = 0) {
    std::vector<QualityArtifactSelection> candidates;
    int requested_resolution = resolve_target_resolution(quality_mode, input_width, input_height);
    DeviceInfo device{"", available_memory_mb, backend};
    int search_resolution = quality_search_resolution(device, quality_mode, requested_resolution);
    bool coarse_to_fine = false;

    if (app::should_use_coarse_to_fine_for_request(device, requested_resolution, fallback_mode,
                                                   coarse_resolution_override)) {
        if (auto coarse_resolution = app::coarse_artifact_resolution_for_request(
                device, requested_resolution, coarse_resolution_override);
            coarse_resolution.has_value()) {
            search_resolution = *coarse_resolution;
            coarse_to_fine = true;
        }
    }

    if (backend == Backend::DirectML && requested_resolution > 1536) {
        requested_resolution = 1536;
    }

    constexpr int kFallbackResolutions[] = {2048, 1536, 1024, 768, 512};
    bool exact_artifact_available = false;
    const bool require_exact_resolution =
        is_fixed_quality_mode(quality_mode) && (!coarse_to_fine || coarse_resolution_override > 0);
    const int exact_resolution = coarse_to_fine ? search_resolution : requested_resolution;
    for (int resolution : kFallbackResolutions) {
        if (resolution > requested_resolution) {
            continue;
        }
        if (require_exact_resolution && resolution != exact_resolution && !exact_artifact_available) {
            continue;
        }

        auto artifact_paths =
            artifact_paths_for_backend(models_root, backend, resolution, quantization_mode);
        bool found_for_resolution = false;
        for (const auto& artifact_path : artifact_paths) {
            if (!path_exists(artifact_path)) {
                continue;
            }
            found_for_resolution = true;
            candidates.push_back(QualityArtifactSelection{artifact_path, requested_resolution,
                                                          resolution,
                                                          resolution != requested_resolution ||
                                                              coarse_to_fine,
                                                          coarse_to_fine});
        }
        if (require_exact_resolution && resolution == exact_resolution) {
            if (!found_for_resolution) {
                return {};
            }
            exact_artifact_available = true;
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
    int input_height, int quantization_mode, std::int64_t available_memory_mb = 0,
    QualityFallbackMode fallback_mode = QualityFallbackMode::Auto,
    int coarse_resolution_override = 0) {
    auto candidates = quality_artifact_candidates(models_root, backend, quality_mode, input_width,
                                                  input_height, quantization_mode,
                                                  available_memory_mb, fallback_mode,
                                                  coarse_resolution_override);
    if (!candidates.empty()) {
        return candidates.front();
    }

    return std::nullopt;
}

}  // namespace corridorkey::ofx
