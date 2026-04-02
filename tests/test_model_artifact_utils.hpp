#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "common/runtime_paths.hpp"

namespace corridorkey::tests {

struct RequiredArtifact {
    std::filesystem::path path = {};
    std::string label = "";
};

inline std::optional<std::string> unusable_model_artifact_reason(
    const std::filesystem::path& path, std::string_view label = "Model artifact") {
    const auto artifact = common::inspect_model_artifact(path);
    if (artifact.usable) {
        return std::nullopt;
    }

    return std::string(label) + " is not usable: " + artifact.detail;
}

inline std::optional<std::string> first_unusable_model_artifact_reason(
    std::initializer_list<RequiredArtifact> artifacts) {
    for (const auto& artifact : artifacts) {
        auto reason = unusable_model_artifact_reason(
            artifact.path, artifact.label.empty() ? "Model artifact" : artifact.label);
        if (reason.has_value()) {
            return reason;
        }
    }

    return std::nullopt;
}

}  // namespace corridorkey::tests
