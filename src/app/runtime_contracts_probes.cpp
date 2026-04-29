// runtime_contracts_probes.cpp
//
// This file holds the runtime-contracts entry points that depend on
// device-detection probes (which transitively pull ONNX Runtime symbols into
// the link line). It is compiled into corridorkey_core only. The pure
// policy entry points stay in runtime_contracts.cpp under corridorkey_common
// so the OFX plugin (which intentionally avoids ONNX Runtime imports to
// dodge Foundry Nuke's host-side CUDA stack collision; see the comment in
// src/CMakeLists.txt declaring corridorkey_common) can call them without
// dragging the inference engine into its address space.
//
// File-local helpers that are also defined inside the anonymous namespace of
// runtime_contracts.cpp are intentionally duplicated here. Both copies are
// trivial and live in their own translation unit's anonymous namespace, so
// there is no ODR conflict and no scope for them to drift -- the duplicated
// helpers are re-evaluated whenever the contract changes by reviewers
// touching this file.

#include "runtime_contracts.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include <corridorkey/engine.hpp>

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

}  // namespace corridorkey

namespace corridorkey::app {

std::optional<PresetDefinition> find_preset_by_selector(const std::string& selector) {
    return corridorkey::find_preset_by_selector(selector);
}

Result<std::filesystem::path> resolve_model_artifact_for_request(
    const std::filesystem::path& model_path, const InferenceParams& params,
    const DeviceInfo& requested_device) {
    const int model_resolution = packaged_model_resolution(model_path).value_or(0);
    const int requested_resolution =
        params.requested_quality_resolution > 0
            ? params.requested_quality_resolution
            : (params.target_resolution > 0 ? params.target_resolution : model_resolution);
    const bool allow_unrestricted_quality_attempt =
        runtime_optimization_profile_for_device(runtime_capabilities(), requested_device)
            .unrestricted_quality_attempt;

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

    const auto validate_resolved_model =
        [&](const std::filesystem::path& resolved_model_path) -> Result<std::filesystem::path> {
        auto refinement_validation =
            validate_refinement_mode_for_artifact(resolved_model_path, params.refinement_mode);
        if (!refinement_validation) {
            return Unexpected<Error>(refinement_validation.error());
        }
        return resolved_model_path;
    };

    if (allow_unrestricted_quality_attempt && is_packaged_corridorkey_model(model_path) &&
        model_resolution > 0 && requested_resolution > model_resolution &&
        !should_use_coarse_to_fine_for_request(
            requested_device, requested_resolution, params.quality_fallback_mode,
            params.coarse_resolution_override, allow_unrestricted_quality_attempt)) {
        auto direct_attempt_path =
            sibling_model_path_for_resolution(model_path, requested_resolution);
        if (direct_attempt_path.empty()) {
            return Unexpected<Error>{Error{
                ErrorCode::InvalidParameters,
                "The requested packaged quality artifact could not be derived from the selected "
                "model.",
            }};
        }
        if (!std::filesystem::exists(direct_attempt_path)) {
            return Unexpected<Error>{Error{
                ErrorCode::ModelLoadFailed,
                "The requested packaged quality artifact is missing: " +
                    direct_attempt_path.string(),
            }};
        }
        return validate_resolved_model(direct_attempt_path);
    }

    if (!should_use_coarse_to_fine_for_request(
            requested_device, requested_resolution, params.quality_fallback_mode,
            params.coarse_resolution_override, allow_unrestricted_quality_attempt)) {
        return validate_resolved_model(model_path);
    }

    if (!is_packaged_corridorkey_model(model_path)) {
        return Unexpected<Error>{Error{
            ErrorCode::InvalidParameters,
            "Explicit --model only supports coarse-to-fine for packaged CorridorKey artifacts. "
            "Use a packaged model or switch --quality-fallback to direct.",
        }};
    }

    auto coarse_resolution_search = search_resolution_for_request(
        requested_device, requested_resolution, params.quality_fallback_mode,
        params.coarse_resolution_override, allow_unrestricted_quality_attempt);
    if (!coarse_resolution_search) {
        return Unexpected<Error>(coarse_resolution_search.error());
    }
    const int coarse_resolution = coarse_resolution_search->first;

    auto coarse_model_path = sibling_model_path_for_resolution(model_path, coarse_resolution);
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

}  // namespace corridorkey::app
