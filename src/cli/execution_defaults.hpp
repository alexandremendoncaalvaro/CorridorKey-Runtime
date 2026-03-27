#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>

#include <corridorkey/types.hpp>

#include "../app/runtime_contracts.hpp"

namespace corridorkey::cli {

[[nodiscard]] inline std::optional<int> packaged_model_resolution(
    const std::filesystem::path& model_path) {
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

[[nodiscard]] inline bool is_packaged_corridorkey_model(const std::filesystem::path& model_path) {
    const std::string filename = model_path.filename().string();
    return filename.rfind("corridorkey_", 0) == 0 && packaged_model_resolution(model_path).has_value();
}

[[nodiscard]] inline std::filesystem::path sibling_model_path_for_resolution(
    const std::filesystem::path& model_path, int resolution) {
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

[[nodiscard]] inline Result<std::filesystem::path> resolve_explicit_model_quality_fallback(
    const std::filesystem::path& model_path, const InferenceParams& params,
    const DeviceInfo& device) {
    const int model_resolution = packaged_model_resolution(model_path).value_or(0);
    const int requested_resolution =
        params.requested_quality_resolution > 0
            ? params.requested_quality_resolution
            : (params.target_resolution > 0 ? params.target_resolution : model_resolution);

    if (!app::should_use_coarse_to_fine_for_request(device, requested_resolution,
                                                    params.quality_fallback_mode,
                                                    params.coarse_resolution_override)) {
        return model_path;
    }

    if (!is_packaged_corridorkey_model(model_path)) {
        return Unexpected<Error>{Error{
            ErrorCode::InvalidParameters,
            "Explicit --model only supports coarse-to-fine for packaged CorridorKey artifacts. "
            "Use a packaged model or switch --quality-fallback to direct.",
        }};
    }

    auto coarse_resolution = app::coarse_artifact_resolution_for_request(
        device, requested_resolution, params.coarse_resolution_override);
    if (!coarse_resolution.has_value() || *coarse_resolution >= requested_resolution) {
        return Unexpected<Error>{Error{
            ErrorCode::InvalidParameters,
            "Explicit --model requested coarse-to-fine, but no smaller coarse artifact could be "
            "resolved for this request.",
        }};
    }

    auto coarse_model_path = sibling_model_path_for_resolution(model_path, *coarse_resolution);
    if (coarse_model_path.empty()) {
        return Unexpected<Error>{Error{
            ErrorCode::InvalidParameters,
            "Explicit --model requested coarse-to-fine, but the packaged coarse artifact name "
            "could not be derived.",
        }};
    }

    if (!std::filesystem::exists(coarse_model_path)) {
        return Unexpected<Error>{Error{
            ErrorCode::ModelLoadFailed,
            "Explicit --model requested coarse-to-fine, but the coarse artifact is missing: " +
                coarse_model_path.string(),
        }};
    }

    return coarse_model_path;
}

}  // namespace corridorkey::cli
