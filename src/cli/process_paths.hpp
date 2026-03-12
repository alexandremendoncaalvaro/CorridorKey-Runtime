#pragma once

#include <corridorkey/types.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace corridorkey::cli {

struct ProcessPaths {
    std::filesystem::path input_path = {};
    std::filesystem::path output_path = {};
};

inline Result<ProcessPaths> resolve_process_paths(
    const std::optional<std::filesystem::path>& input_option,
    const std::optional<std::filesystem::path>& output_option,
    const std::vector<std::string>& positional_arguments) {
    ProcessPaths resolved;

    if (input_option.has_value()) {
        resolved.input_path = *input_option;

        if (positional_arguments.size() > 1) {
            return Unexpected<Error>{
                Error{ErrorCode::InvalidParameters,
                      "'process' accepts at most one positional output path when '--input' is "
                      "provided."}};
        }

        if (output_option.has_value()) {
            resolved.output_path = *output_option;
        } else if (!positional_arguments.empty()) {
            resolved.output_path = positional_arguments.front();
        }

        return resolved;
    }

    if (positional_arguments.size() > 2) {
        return Unexpected<Error>{Error{ErrorCode::InvalidParameters,
                                       "'process' accepts at most two positional paths: input "
                                       "and output."}};
    }

    if (!positional_arguments.empty()) {
        resolved.input_path = positional_arguments.front();
    }
    if (output_option.has_value()) {
        resolved.output_path = *output_option;
    } else if (positional_arguments.size() >= 2) {
        resolved.output_path = positional_arguments[1];
    }

    return resolved;
}

}  // namespace corridorkey::cli
