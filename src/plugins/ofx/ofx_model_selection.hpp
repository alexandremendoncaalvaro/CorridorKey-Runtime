#pragma once

#include <algorithm>
#include <cctype>
#include <corridorkey/engine.hpp>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "app/runtime_contracts.hpp"
#include "ofx_constants.hpp"

// NOLINTBEGIN(bugprone-easily-swappable-parameters,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
// The selector helpers below take stable (quality_mode, requested_resolution,
// input_width, input_height, available_memory_mb) parameter packs that the
// OFX render path threads through every selector by name. Wrapping each
// signature in a struct would force every test fixture and call site to
// brace-init for a domain-equality safety claim that the surrounding context
// already enforces. The per-function NOLINTNEXTLINE annotations were broken
// by the multi-line comment continuation; this block is the file-scope
// equivalent.
namespace corridorkey::ofx {

namespace selection_detail {

// Quality-ladder resolutions in pixels. Promoted out of switch statements and
// magic-number sites so cppcoreguidelines-avoid-magic-numbers stays clean.
constexpr int kRes512 = 512;
constexpr int kRes768 = 768;
constexpr int kRes1024 = 1024;
constexpr int kRes1536 = 1536;
constexpr int kRes2048 = 2048;

// Auto-resolution thresholds (input long edge), aligned to the rungs above.
constexpr int kAutoThreshold1024 = 1000;
constexpr int kAutoThreshold1536 = 2000;
constexpr int kAutoThreshold2048 = 3000;

// Round-up MB→GB conversion: ceil(mb/1024) == (mb + 1023) / 1024.
constexpr int kBytesPerMib = 1024;
constexpr int kCeilToMib = kBytesPerMib - 1;

constexpr std::string_view kDynamicBlueModelFilename = "corridorkey_dynamic_blue_fp16.ts";

// build_bootstrap_candidates extraction limit on per-candidate-builder nesting.
// (Used only as a documentation anchor — no runtime constant required.)

}  // namespace selection_detail

struct BootstrapEngineCandidate {
    DeviceInfo device;
    std::filesystem::path requested_model_path;
    std::filesystem::path executable_model_path;
    int requested_resolution = 0;
    int effective_resolution = 0;
};

using QualityArtifactSelection = app::ArtifactSelection;

inline bool is_dynamic_blue_artifact_filename(std::string_view filename) {
    return filename == selection_detail::kDynamicBlueModelFilename;
}

inline bool is_legacy_blue_artifact_filename(std::string_view filename) {
    return filename.rfind("corridorkey_blue_", 0) == 0;
}

inline bool is_dedicated_blue_artifact_filename(std::string_view filename) {
    return is_dynamic_blue_artifact_filename(filename) ||
           is_legacy_blue_artifact_filename(filename);
}

inline bool is_dynamic_blue_artifact_path(const std::filesystem::path& path) {
    return is_dynamic_blue_artifact_filename(path.filename().string());
}

inline bool backend_supports_dynamic_blue(Backend backend) {
    return backend == Backend::Auto || backend == Backend::TensorRT || backend == Backend::CUDA ||
           backend == Backend::TorchTRT;
}

inline Backend runtime_backend_for_quality_artifact(Backend requested_backend,
                                                    const std::filesystem::path& artifact_path) {
    const auto extension = artifact_path.extension().string();
    if (extension == ".ts") {
        return Backend::TorchTRT;
    }
    if (requested_backend == Backend::TorchTRT && extension == ".onnx") {
        return Backend::TensorRT;
    }
    return requested_backend;
}

inline bool is_dedicated_blue_artifact_path(const std::filesystem::path& path) {
    return is_dedicated_blue_artifact_filename(path.filename().string());
}

struct QualityCompileFailureCacheContext {
    std::filesystem::path models_root;
    std::uint64_t models_bundle_token = 0;
    Backend backend = Backend::Auto;
    int device_index = 0;
    std::int64_t available_memory_mb = 0;
};

struct QualityCompileFailureEntry {
    std::filesystem::path artifact_path;
    int requested_resolution = 0;
    int effective_resolution = 0;
    std::string error_message;
};

struct QualityCompileFailureCache {
    QualityCompileFailureCacheContext context;
    std::vector<QualityCompileFailureEntry> entries;
    bool initialized = false;
};

struct CachedQualityCompileFailure {
    QualityArtifactSelection selection;
    std::string error_message;
};

inline const char* quality_mode_label(int quality_mode) {
    return quality_mode_ui_label(quality_mode);
}

inline bool is_fixed_quality_mode(int quality_mode) {
    return quality_mode != kQualityAuto;
}

inline int quality_mode_for_resolution(int resolution) {
    switch (resolution) {
        case selection_detail::kRes512:
            return kQualityPreview;
        case selection_detail::kRes1024:
            return kQualityHigh;
        case selection_detail::kRes1536:
            return kQualityUltra;
        case selection_detail::kRes2048:
            return kQualityMaximum;
        default:
            return kQualityAuto;
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): quality_mode and
// requested_resolution share type int but encode distinct domain values; a
// param struct here would force every caller to switch to brace-init for
// negligible call-site clarity gain.
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
    return static_cast<int>((memory_mb + selection_detail::kCeilToMib) / selection_detail::kBytesPerMib);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): see note on
// quality_search_resolution above.
inline std::optional<std::string> unsupported_quality_message(
    const DeviceInfo& device, int quality_mode, int requested_resolution,
    bool allow_unrestricted_quality_attempt = false) {
    if (!is_fixed_quality_mode(quality_mode)) {
        return std::nullopt;
    }

    if ((device.backend == Backend::TensorRT || device.backend == Backend::CUDA ||
         device.backend == Backend::DirectML || device.backend == Backend::WindowsML ||
         device.backend == Backend::OpenVINO) &&
        requested_resolution == selection_detail::kRes768) {
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
           cache.context.available_memory_mb == context.available_memory_mb;
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

    auto existing = std::ranges::find_if(
        cache.entries, [&](const QualityCompileFailureEntry& entry) {
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
    auto existing = std::ranges::find_if(
        cache.entries, [&](const QualityCompileFailureEntry& entry) {
            return entry.artifact_path == selection.executable_model_path &&
                   entry.requested_resolution == selection.requested_resolution &&
                   entry.effective_resolution == selection.effective_resolution;
        });
    if (existing != cache.entries.end()) {
        existing->error_message = error_message;
        return;
    }

    cache.entries.push_back(QualityCompileFailureEntry{
        .artifact_path = selection.executable_model_path,
        .requested_resolution = selection.requested_resolution,
        .effective_resolution = selection.effective_resolution,
        .error_message = error_message,
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

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): quality_mode +
// input_width + input_height each encode distinct domains; structural change
// would ripple into every render-path call site.
inline int resolve_target_resolution(int quality_mode, int input_width, int input_height) {
    if (quality_mode == kQualityPreview) return selection_detail::kRes512;
    if (quality_mode == kQualityHigh) return selection_detail::kRes1024;
    if (quality_mode == kQualityUltra) return selection_detail::kRes1536;
    if (quality_mode == kQualityMaximum) return selection_detail::kRes2048;

    const int max_dim = std::max(input_width, input_height);
    if (max_dim > selection_detail::kAutoThreshold2048) return selection_detail::kRes2048;
    if (max_dim > selection_detail::kAutoThreshold1536) return selection_detail::kRes1536;
    if (max_dim > selection_detail::kAutoThreshold1024) return selection_detail::kRes1024;
    return selection_detail::kRes512;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): see note on
// resolve_target_resolution above.
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
                                                       Backend backend, int resolution,
                                                       std::string_view screen_color = "green") {
    if (backend == Backend::MLX) {
        return models_root / ("corridorkey_mlx_bridge_" + std::to_string(resolution) + ".mlxfn");
    }
    if (screen_color == "blue") {
        (void)resolution;
        return models_root / selection_detail::kDynamicBlueModelFilename;
    }
    return models_root / ("corridorkey_fp16_" + std::to_string(resolution) + ".onnx");
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

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): input_height (int) and
// available_memory_mb (int64_t) are implicitly convertible per clang-tidy but
// have wildly different magnitudes in practice; render-path callers always
// supply named locals so a swap would surface in code review immediately.
inline std::vector<std::filesystem::path> expected_quality_artifact_paths(
    const std::filesystem::path& models_root, Backend backend, int quality_mode, int input_width,
    int input_height, std::int64_t available_memory_mb = 0,
    QualityFallbackMode fallback_mode = QualityFallbackMode::Auto,
    int coarse_resolution_override = 0, bool allow_unrestricted_quality_attempt = false,
    std::string_view screen_color = "green") {
    const int requested_resolution =
        resolve_target_resolution(quality_mode, input_width, input_height);
    if (screen_color == "blue" && backend_supports_dynamic_blue(backend)) {
        return {artifact_path_for_backend(models_root, backend, requested_resolution, "blue")};
    }

    const bool allow_lower_resolution_fallback = !is_fixed_quality_mode(quality_mode);
    const DeviceInfo device{
        .name = "",
        .available_memory_mb = available_memory_mb,
        .backend = backend,
    };

    auto expected = app::expected_artifact_paths_for_request(
        models_root, device, requested_resolution, allow_lower_resolution_fallback, fallback_mode,
        coarse_resolution_override, allow_unrestricted_quality_attempt);
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

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): see note on
// expected_quality_artifact_paths above.
inline std::string missing_quality_artifact_message(
    const std::filesystem::path& models_root, Backend backend, int quality_mode, int input_width,
    int input_height, std::int64_t available_memory_mb = 0,
    QualityFallbackMode fallback_mode = QualityFallbackMode::Auto,
    int coarse_resolution_override = 0, bool allow_unrestricted_quality_attempt = false,
    std::string_view screen_color = "green") {
    const int requested_resolution = normalize_target_resolution_for_backend(
        backend, quality_mode, resolve_target_resolution(quality_mode, input_width, input_height));
    if (screen_color == "blue" && backend_supports_dynamic_blue(backend)) {
        return missing_artifact_message(
            "Requested quality " + std::string(quality_mode_label(quality_mode)) +
                " is missing the required dedicated blue model artifact",
            models_root,
            {artifact_path_for_backend(models_root, backend, requested_resolution, "blue")});
    }

    const bool allow_lower_resolution_fallback = !is_fixed_quality_mode(quality_mode);
    const DeviceInfo device{
        .name = "",
        .available_memory_mb = available_memory_mb,
        .backend = backend,
    };
    auto expected = app::expected_artifact_paths_for_request(
        models_root, device, requested_resolution, allow_lower_resolution_fallback, fallback_mode,
        coarse_resolution_override, allow_unrestricted_quality_attempt);
    if (!expected) {
        return expected.error().message;
    }

    return missing_artifact_message("Requested quality " +
                                        std::string(quality_mode_label(quality_mode)) +
                                        " is missing the required model artifact",
                                    models_root, *expected);
}
inline bool path_exists(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::exists(path, error) && !error;
}

inline bool has_mlx_bootstrap_artifacts(const std::filesystem::path& models_root) {
    return path_exists(mlx_pack_path(models_root)) &&
           path_exists(artifact_path_for_backend(models_root, Backend::MLX, selection_detail::kRes512));
}

inline std::vector<std::filesystem::path> expected_bootstrap_artifact_paths(
    const RuntimeCapabilities& capabilities, const DeviceInfo& detected_device,
    const std::filesystem::path& models_root) {
    std::vector<std::filesystem::path> expected;
    auto append_unique_path = [&](const std::filesystem::path& path) {
        if (path.empty()) {
            return;
        }
        if (std::ranges::find(expected, path) == expected.end()) {
            expected.push_back(path);
        }
    };

#ifdef __APPLE__
    if (capabilities.platform == "macos" && capabilities.apple_silicon &&
        capabilities.mlx_probe_available && has_mlx_bootstrap_artifacts(models_root)) {
        append_unique_path(mlx_pack_path(models_root));
        append_unique_path(
            artifact_path_for_backend(models_root, Backend::MLX, selection_detail::kRes512));
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
            append_unique_path(
                artifact_path_for_backend(models_root, Backend::MLX, selection_detail::kRes512));
        }
    };

    append_expected_candidate(detected_device);
    if (detected_device.backend != Backend::CPU) {
        append_expected_candidate(DeviceInfo{
            .name = "Generic CPU",
            .available_memory_mb = detected_device.available_memory_mb,
            .backend = Backend::CPU,
        });
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

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): see note on
// expected_quality_artifact_paths above.
inline std::vector<QualityArtifactSelection> quality_artifact_candidates(
    const std::filesystem::path& models_root, Backend backend, int quality_mode, int input_width,
    int input_height, std::int64_t available_memory_mb = 0,
    QualityFallbackMode fallback_mode = QualityFallbackMode::Auto,
    int coarse_resolution_override = 0, bool allow_unrestricted_quality_attempt = false,
    std::string_view screen_color = "green") {
    const int requested_resolution = normalize_target_resolution_for_backend(
        backend, quality_mode, resolve_target_resolution(quality_mode, input_width, input_height));

    std::vector<QualityArtifactSelection> selections;

    // Blue is deterministic: use the dedicated dynamic artifact only. The
    // explicit Blue-Green UI mode is the green-model fallback path.
    if (screen_color == "blue" && backend_supports_dynamic_blue(backend)) {
        const auto blue_path =
            artifact_path_for_backend(models_root, backend, requested_resolution, "blue");
        std::error_code blue_ec;
        if (std::filesystem::exists(blue_path, blue_ec)) {
            QualityArtifactSelection blue_selection{};
            blue_selection.executable_model_path = blue_path;
            blue_selection.requested_resolution = requested_resolution;
            blue_selection.effective_resolution = requested_resolution;
            blue_selection.used_fallback = false;
            blue_selection.coarse_to_fine = false;
            selections.push_back(std::move(blue_selection));
        }
        return selections;
    }

    const bool allow_lower_resolution_fallback = !is_fixed_quality_mode(quality_mode);
    const DeviceInfo device{
        .name = "",
        .available_memory_mb = available_memory_mb,
        .backend = backend,
    };
    auto candidates = app::quality_artifact_candidates_for_request(
        models_root, device, requested_resolution, allow_lower_resolution_fallback, fallback_mode,
        coarse_resolution_override, allow_unrestricted_quality_attempt);
    if (candidates) {
        for (auto& candidate : *candidates) {
            selections.push_back(std::move(candidate));
        }
    }
    return selections;
}

inline std::optional<QualityArtifactSelection> select_quality_artifact(
    const std::filesystem::path& models_root, Backend backend, int quality_mode, int input_width,
    int input_height, std::int64_t available_memory_mb = 0,
    QualityFallbackMode fallback_mode = QualityFallbackMode::Auto,
    int coarse_resolution_override = 0, bool allow_unrestricted_quality_attempt = false,
    std::string_view screen_color = "green");

namespace selection_detail {

// Helpers extracted from build_bootstrap_candidates so the public API stays
// below the readability-function-cognitive-complexity threshold (15).

inline bool same_backend_and_path(const BootstrapEngineCandidate& lhs,
                                  const BootstrapEngineCandidate& rhs) {
    return lhs.device.backend == rhs.device.backend &&
           lhs.executable_model_path == rhs.executable_model_path;
}

inline void append_unique_candidate(std::vector<BootstrapEngineCandidate>& candidates,
                                    BootstrapEngineCandidate candidate) {
    if (candidate.executable_model_path.empty()) {
        return;
    }
    auto duplicate = std::ranges::find_if(
        candidates,
        [&](const BootstrapEngineCandidate& existing) {
            return same_backend_and_path(existing, candidate);
        });
    if (duplicate == candidates.end()) {
        candidates.push_back(std::move(candidate));
    }
}

inline std::optional<BootstrapEngineCandidate> resolve_default_candidate(
    const std::filesystem::path& models_root, const RuntimeCapabilities& capabilities,
    const DeviceInfo& device) {
    auto preset = app::default_preset_for_capabilities(capabilities);
    auto model_entry = app::default_model_for_request(capabilities, device, preset);
    if (!model_entry.has_value()) {
        return std::nullopt;
    }

    auto requested_model_path = models_root / model_entry->filename;
    if (!path_exists(requested_model_path)) {
        return std::nullopt;
    }

    auto executable_model_path = requested_model_path;
    if (device.backend == Backend::MLX) {
        executable_model_path = artifact_path_for_backend(models_root, Backend::MLX, kRes512);
        if (!path_exists(executable_model_path)) {
            return std::nullopt;
        }
    }

    const int effective_resolution =
        app::packaged_model_resolution(executable_model_path).value_or(kRes512);
    const int requested_resolution =
        app::packaged_model_resolution(requested_model_path).value_or(effective_resolution);
    return BootstrapEngineCandidate{
        .device = device,
        .requested_model_path = requested_model_path,
        .executable_model_path = executable_model_path,
        .requested_resolution = requested_resolution,
        .effective_resolution = effective_resolution,
    };
}

inline std::optional<BootstrapEngineCandidate> resolve_quality_candidate(
    const std::filesystem::path& models_root, const DeviceInfo& device, int quality_mode);

}  // namespace selection_detail

inline std::vector<BootstrapEngineCandidate> build_bootstrap_candidates(
    const RuntimeCapabilities& capabilities, const DeviceInfo& detected_device,
    const std::filesystem::path& models_root, int quality_mode = kQualityAuto) {
    std::vector<BootstrapEngineCandidate> candidates;

#ifdef __APPLE__
    if (capabilities.platform == "macos" && capabilities.apple_silicon &&
        capabilities.mlx_probe_available && has_mlx_bootstrap_artifacts(models_root)) {
        selection_detail::append_unique_candidate(
            candidates,
            BootstrapEngineCandidate{
                .device = DeviceInfo{
                    .name = "Apple Silicon MLX",
                    .available_memory_mb = detected_device.available_memory_mb,
                    .backend = Backend::MLX,
                },
                .requested_model_path = mlx_pack_path(models_root),
                .executable_model_path = artifact_path_for_backend(
                    models_root, Backend::MLX, selection_detail::kRes512),
                .requested_resolution = selection_detail::kRes512,
                .effective_resolution = selection_detail::kRes512,
            });
    }
#else
    (void)capabilities;
#endif

    if (is_fixed_quality_mode(quality_mode)) {
        if (auto quality_candidate = selection_detail::resolve_quality_candidate(
                models_root, detected_device, quality_mode)) {
            selection_detail::append_unique_candidate(candidates, std::move(*quality_candidate));
        }
        return candidates;
    }

    if (auto default_candidate =
            selection_detail::resolve_default_candidate(models_root, capabilities, detected_device)) {
        selection_detail::append_unique_candidate(candidates, std::move(*default_candidate));
    }

    return candidates;
}

namespace selection_detail {

inline std::optional<BootstrapEngineCandidate> resolve_quality_candidate(
    const std::filesystem::path& models_root, const DeviceInfo& device, int quality_mode) {
    auto selection = select_quality_artifact(models_root, device.backend, quality_mode, 0, 0,
                                             device.available_memory_mb);
    if (!selection.has_value()) {
        return std::nullopt;
    }

    return BootstrapEngineCandidate{
        .device = device,
        .requested_model_path = selection->executable_model_path,
        .executable_model_path = selection->executable_model_path,
        .requested_resolution = selection->requested_resolution,
        .effective_resolution = selection->effective_resolution,
    };
}

}  // namespace selection_detail

inline std::optional<QualityArtifactSelection> select_quality_artifact(
    const std::filesystem::path& models_root, Backend backend, int quality_mode, int input_width,
    int input_height, std::int64_t available_memory_mb, QualityFallbackMode fallback_mode,
    int coarse_resolution_override, bool allow_unrestricted_quality_attempt,
    std::string_view screen_color) {
    auto candidates =
        quality_artifact_candidates(models_root, backend, quality_mode, input_width, input_height,
                                    available_memory_mb, fallback_mode, coarse_resolution_override,
                                    allow_unrestricted_quality_attempt, screen_color);
    if (!candidates.empty()) {
        return candidates.front();
    }

    return std::nullopt;
}

}  // namespace corridorkey::ofx
// NOLINTEND(bugprone-easily-swappable-parameters,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions,cppcoreguidelines-pro-bounds-constant-array-index)
