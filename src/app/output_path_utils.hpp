#pragma once

#include <filesystem>

namespace corridorkey::app {

inline std::filesystem::path normalize_runtime_output_path(
    const std::filesystem::path& output_path, const std::filesystem::path& working_directory = {}) {
    if (output_path.empty() || output_path.has_root_path() || output_path.extension().empty()) {
        return output_path;
    }

    if (!output_path.parent_path().empty()) {
        return output_path;
    }

    std::filesystem::path base_directory = working_directory;
    if (base_directory.empty()) {
        std::error_code error;
        base_directory = std::filesystem::current_path(error);
        if (error) {
            return output_path;
        }
    }

    return base_directory / output_path;
}

}  // namespace corridorkey::app
