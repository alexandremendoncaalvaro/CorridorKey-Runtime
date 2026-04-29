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

using QualityArtifactSelection = app::ArtifactSelection;

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

inline bool is_fixed_quality_mode(int quality_mode) {
    return quality_mode != kQualityAuto;
}

inline int quality_mode_for_resolution(int resolution) {
    switch (resolution) {
        case 512:
            return kQualityPreview;
        case 1024:
            return kQualityHigh;
        case 1536:
            return kQualityUltra;
        case 2048:
            return kQualityMaximum;
        default:
            return kQualityAuto;
    }
}

inline int quality_search_resolution(const DeviceInfo& device, int quality_mode,
                                     int requested_resolution) {
    if (is_fixed_quality_mode(quality_mode)) {
        return requested_resolution;
    }

    if (auto max_resolution = app::max_supported_resolution_for_device(device);
        max_resolution.has_value()) {
        return std::min(requested_resolution, *max_resolution);
    }

    return requested_resolution;
}

inline int rounded_gb_from_mb(std::int64_t memory_mb) {
    return static_cast<int>((memory_mb + 1023) / 1024);
}

inline std::optional<std::string> unsupported_quality_message(
    const DeviceInfo& device, int quality_mode, int requested_resolution,
    bool allow_unrestricted_quality_attempt = false) {
    if (!is_fixed_quality_mode(quality_mode)) {
        return std::nullopt;
    }

    if ((device.backend == Backend::TensorRT || device.backend == Backend::CUDA ||
         device.backend == Backend::DirectML || device.backend == Backend::WindowsML ||
         device.backend == Backend::OpenVINO) &&
        requested_resolution == 768) {
        return "768px is not part of CorridorKey's current public Windows quality ladder. "
               "Please use Draft (512) or High (1024).";
    }

    auto max_supported_resolution = app::max_supported_resolution_for_device(device);
    if (!max_supported_resolution.has_value() ||
        requested_resolution <= *max_supported_resolution) {
        return std::nullopt;
    }
    if (allow_unrestricted_quality_attempt) {
        return std::nullopt;
    }

    auto minimum_memory_mb =
        app::minimum_supported_memory_mb_for_resolution(device.backend, requested_resolution);
    if (!minimum_memory_mb.has_value() || device.available_memory_mb <= 0) {
        return std::nullopt;
    }

    return std::string(quality_mode_label(quality_mode)) + " requires at least " +
           std::to_string(rounded_gb_from_mb(*minimum_memory_mb)) +
           " GB of VRAM for CorridorKey's current safe quality ceiling on " +
           (device.backend == Backend::TensorRT ? std::string("the Windows RTX path")
                                                : std::string("the selected backend")) +
           ". This GPU reports " + std::to_string(rounded_gb_from_mb(device.available_memory_mb)) +
           " GB, so the safe quality ceiling is " +
           quality_mode_label(quality_mode_for_resolution(*max_supported_resolution)) + ".";
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

inline bool use_quality_compile_failure_cache(Backend backend) {
    return backend == Backend::TensorRT;
}

inline bool quality_compile_failure_cache_matches(
    const QualityCompileFailureCache& cache, const QualityCompileFailureCacheContext& context) {
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
    const QualityCompileFailureCache& cache, const QualityCompileFailureCacheContext& context,
    const QualityArtifactSelection& selection) {
    if (!use_quality_compile_failure_cache(context.backend) ||
        !quality_compile_failure_cache_matches(cache, context)) {
        return std::nullopt;
    }

    auto existing = std::find_if(
        cache.entries.begin(), cache.entries.end(), [&](const QualityCompileFailureEntry& entry) {
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
        cache.entries.begin(), cache.entries.end(), [&](const QualityCompileFailureEntry& entry) {
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
    const QualityCompileFailureCache& cache, const QualityCompileFailureCacheContext& context) {
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

inline std::optional<std::string> unsupported_quantization_message(
    Backend backend, int quantization_mode, bool allow_cpu_fallback = false) {
    if (quantization_mode != kQuantizationInt8) {
        return std::nullopt;
    }

    if ((backend == Backend::TensorRT || backend == Backend::CUDA) && !allow_cpu_fallback) {
        return "INT8 (Experimental) is currently CPU-only on the Windows RTX track. "
               "Enable Allow CPU Fallback or use FP16 (Official).";
    }

    if ((backend == Backend::DirectML || backend == Backend::WindowsML ||
         backend == Backend::OpenVINO) &&
        !allow_cpu_fallback) {
        return "INT8 (Experimental) is not yet validated on the selected Windows GPU backend. "
               "Enable Allow CPU Fallback or use FP16 (Official).";
    }

    return std::nullopt;
}

inline int clamp_quality_mode_for_cpu_backend(Backend backend, int quality_mode) {
    if (backend == Backend::CPU) {
        return kQualityPreview;
    }
    return quality_mode;
}

inline int resolve_target_resolution(int quality_mode, int input_width, int input_height) {
    if (quality_mode == kQualityPreview) return 512;
    if (quality_mode == kQualityHigh) return 1024;
    if (quality_mode == kQualityUltra) return 1536;
    if (quality_mode == kQualityMaximum) return 2048;

    int max_dim = std::max(input_width, input_height);
    if (max_dim > 3000) return 2048;
    if (max_dim > 2000) return 1536;
    if (max_dim > 1000) return 1024;
    return 512;
}

inline int normalize_target_resolution_for_backend(Backend backend, int quality_mode,
                                                   int requested_resolution) {
    (void)backend;
    (void)quality_mode;
    return requested_resolution;
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

inline app::ArtifactVariantPreference artifact_variant_preference(int quantization_mode) {
    switch (quantization_mode) {
        case kQuantizationFp16:
            return app::ArtifactVariantPreference::FP16;
        case kQuantizationInt8:
            return app::ArtifactVariantPreference::Int8;
        default:
            return app::ArtifactVariantPreference::Auto;
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
    int coarse_resolution_override = 0, bool allow_unrestricted_quality_attempt = false) {
    const int effective_quality_mode = clamp_quality_mode_for_cpu_backend(backend, quality_mode);
    const int requested_resolution =
        resolve_target_resolution(effective_quality_mode, input_width, input_height);
    const bool allow_lower_resolution_fallback = !is_fixed_quality_mode(effective_quality_mode);
    DeviceInfo device{"", available_memory_mb, backend};

    auto expected = app::expected_artifact_paths_for_request(
        models_root, device, requested_resolution, artifact_variant_preference(quantization_mode),
        allow_lower_resolution_fallback, fallback_mode, coarse_resolution_override,
        allow_unrestricted_quality_attempt);
    if (!expected) {
        return {};
    }

    return *expected;
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
    int coarse_resolution_override = 0, bool allow_unrestricted_quality_attempt = false) {
    const int effective_quality_mode = clamp_quality_mode_for_cpu_backend(backend, quality_mode);
    const int requested_resolution = normalize_target_resolution_for_backend(
        backend, effective_quality_mode,
        resolve_target_resolution(effective_quality_mode, input_width, input_height));
    const bool allow_lower_resolution_fallback = !is_fixed_quality_mode(effective_quality_mode);
    DeviceInfo device{"", available_memory_mb, backend};
    auto expected = app::expected_artifact_paths_for_request(
        models_root, device, requested_resolution, artifact_variant_preference(quantization_mode),
        allow_lower_resolution_fallback, fallback_mode, coarse_resolution_override,
        allow_unrestricted_quality_attempt);
    if (!expected) {
        return expected.error().message;
    }

    const auto& expected_artifacts = *expected;
    if (cpu_quality_guardrail_active) {
        return missing_artifact_message(
            "CPU backend is limited to Draft (512), but the required model artifact is missing",
            models_root, expected_artifacts);
    }

    return missing_artifact_message("Requested quality " +
                                        std::string(quality_mode_label(quality_mode)) +
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

inline std::vector<std::filesystem::path> expected_bootstrap_artifact_paths(
    const RuntimeCapabilities& capabilities, const DeviceInfo& detected_device,
    const std::filesystem::path& models_root) {
    std::vector<std::filesystem::path> expected;
    auto append_unique_path = [&](const std::filesystem::path& path) {
        if (path.empty()) {
            return;
        }
        if (std::find(expected.begin(), expected.end(), path) == expected.end()) {
            expected.push_back(path);
        }
    };

#if defined(__APPLE__)
    if (capabilities.platform == "macos" && capabilities.apple_silicon &&
        capabilities.mlx_probe_available && has_mlx_bootstrap_artifacts(models_root)) {
        append_unique_path(mlx_pack_path(models_root));
        append_unique_path(artifact_path_for_backend(models_root, Backend::MLX, 512));
    }
#else
    (void)capabilities;
#endif

    auto preset = app::default_preset_for_capabilities(capabilities);
    auto append_expected_candidate = [&](const DeviceInfo& device) {
        auto model_entry = app::default_model_for_request(capabilities, device, preset);
        if (!model_entry.has_value()) {
            return;
        }

        auto requested_model_path = models_root / model_entry->filename;
        append_unique_path(requested_model_path);

        if (device.backend == Backend::MLX) {
            append_unique_path(artifact_path_for_backend(models_root, Backend::MLX, 512));
        }
    };

    append_expected_candidate(detected_device);
    if (detected_device.backend != Backend::CPU) {
        append_expected_candidate(
            DeviceInfo{"Generic CPU", detected_device.available_memory_mb, Backend::CPU});
    }

    return expected;
}

inline std::string missing_bootstrap_artifact_message(const RuntimeCapabilities& capabilities,
                                                      const DeviceInfo& detected_device,
                                                      const std::filesystem::path& models_root) {
    return missing_artifact_message(
        "No compatible bootstrap model artifact was found for this device", models_root,
        expected_bootstrap_artifact_paths(capabilities, detected_device, models_root));
}

inline std::vector<QualityArtifactSelection> quality_artifact_candidates(
    const std::filesystem::path& models_root, Backend backend, int quality_mode, int input_width,
    int input_height, int quantization_mode, std::int64_t available_memory_mb = 0,
    QualityFallbackMode fallback_mode = QualityFallbackMode::Auto,
    int coarse_resolution_override = 0, bool allow_unrestricted_quality_attempt = false) {
    const int requested_resolution = normalize_target_resolution_for_backend(
        backend, quality_mode, resolve_target_resolution(quality_mode, input_width, input_height));
    const bool allow_lower_resolution_fallback = !is_fixed_quality_mode(quality_mode);
    DeviceInfo device{"", available_memory_mb, backend};
    auto candidates = app::quality_artifact_candidates_for_request(
        models_root, device, requested_resolution, artifact_variant_preference(quantization_mode),
        allow_lower_resolution_fallback, fallback_mode, coarse_resolution_override,
        allow_unrestricted_quality_attempt);
    if (!candidates) {
        return {};
    }
    return *candidates;
}

inline std::optional<QualityArtifactSelection> select_quality_artifact(
    const std::filesystem::path& models_root, Backend backend, int quality_mode, int input_width,
    int input_height, int quantization_mode, std::int64_t available_memory_mb = 0,
    QualityFallbackMode fallback_mode = QualityFallbackMode::Auto,
    int coarse_resolution_override = 0, bool allow_unrestricted_quality_attempt = false);

inline std::vector<BootstrapEngineCandidate> build_bootstrap_candidates(
    const RuntimeCapabilities& capabilities, const DeviceInfo& detected_device,
    const std::filesystem::path& models_root, int quality_mode = kQualityAuto,
    int quantization_mode = kDefaultQuantizationMode) {
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

    auto append_quality_candidate = [&](const DeviceInfo& device) {
        auto selection = select_quality_artifact(models_root, device.backend, quality_mode, 0, 0,
                                                 quantization_mode, device.available_memory_mb);
        if (!selection.has_value()) {
            return;
        }

        append_unique({device, selection->executable_model_path, selection->executable_model_path,
                       selection->requested_resolution, selection->effective_resolution});
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

    if (is_fixed_quality_mode(quality_mode)) {
        append_quality_candidate(detected_device);
        if (detected_device.backend != Backend::CPU) {
            append_quality_candidate(
                DeviceInfo{"Generic CPU", detected_device.available_memory_mb, Backend::CPU});
        }
        return candidates;
    }

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

        int effective_resolution =
            app::packaged_model_resolution(executable_model_path).value_or(512);
        int requested_resolution =
            app::packaged_model_resolution(requested_model_path).value_or(effective_resolution);
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
    int input_height, int quantization_mode, std::int64_t available_memory_mb,
    QualityFallbackMode fallback_mode, int coarse_resolution_override,
    bool allow_unrestricted_quality_attempt) {
    auto candidates =
        quality_artifact_candidates(models_root, backend, quality_mode, input_width, input_height,
                                    quantization_mode, available_memory_mb, fallback_mode,
                                    coarse_resolution_override, allow_unrestricted_quality_attempt);
    if (!candidates.empty()) {
        return candidates.front();
    }

    return std::nullopt;
}

}  // namespace corridorkey::ofx
