#include "mlx_probe.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#if CORRIDORKEY_WITH_MLX
#include <mlx/mlx.h>
#endif

#include "common/runtime_paths.hpp"

namespace corridorkey::core {

bool mlx_probe_available() {
#if CORRIDORKEY_WITH_MLX
    return true;
#else
    return false;
#endif
}

Result<void> probe_mlx_weights(const std::filesystem::path& weights_path) {
    const auto artifact = common::inspect_model_artifact(weights_path);
    if (!artifact.found) {
        return Unexpected<Error>{Error{ErrorCode::ModelLoadFailed,
                                       "MLX weights artifact not found: " + weights_path.string()}};
    }
    if (!artifact.usable) {
        return Unexpected<Error>{Error{ErrorCode::ModelLoadFailed, artifact.detail}};
    }

    std::error_code error;
    const auto file_size = std::filesystem::file_size(weights_path, error);
    if (error || file_size < 10) {
        return Unexpected<Error>{Error{ErrorCode::ModelLoadFailed,
                                       "MLX weights artifact is too small to be a valid "
                                       "safetensors file: " +
                                           weights_path.string()}};
    }

    std::ifstream file(weights_path, std::ios::binary);
    if (!file) {
        return Unexpected<Error>{
            Error{ErrorCode::ModelLoadFailed,
                  "Failed to open MLX weights artifact: " + weights_path.string()}};
    }

    std::uint64_t header_size = 0;
    file.read(reinterpret_cast<char*>(&header_size), sizeof(header_size));
    if (!file || header_size == 0 || header_size > file_size - sizeof(header_size)) {
        return Unexpected<Error>{Error{
            ErrorCode::ModelLoadFailed,
            "Invalid safetensors header size in MLX weights artifact: " + weights_path.string()}};
    }

    char first_non_space = '\0';
    for (std::uint64_t offset = 0; offset < header_size; ++offset) {
        char ch = '\0';
        file.read(&ch, 1);
        if (!file) {
            return Unexpected<Error>{
                Error{ErrorCode::ModelLoadFailed,
                      "Failed to read safetensors header from: " + weights_path.string()}};
        }
        if (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t') {
            first_non_space = ch;
            break;
        }
    }

    if (first_non_space != '{') {
        return Unexpected<Error>{
            Error{ErrorCode::ModelLoadFailed,
                  "MLX weights artifact does not contain a JSON safetensors header: " +
                      weights_path.string()}};
    }

    return {};
}

Result<void> probe_mlx_function(const std::filesystem::path& function_path) {
    const auto artifact = common::inspect_model_artifact(function_path);
    if (!artifact.found) {
        return Unexpected<Error>{
            Error{ErrorCode::ModelLoadFailed,
                  "MLX function artifact not found: " + function_path.string()}};
    }
    if (!artifact.usable) {
        return Unexpected<Error>{Error{ErrorCode::ModelLoadFailed, artifact.detail}};
    }

#if !CORRIDORKEY_WITH_MLX
    return Unexpected<Error>{
        Error{ErrorCode::HardwareNotSupported,
              "MLX probe support is not linked in this build. Install MLX and reconfigure CMake."}};
#else
    try {
        auto imported = mlx::core::import_function(function_path.string());
        (void)imported;
        return {};
    } catch (const std::exception& error) {
        return Unexpected<Error>{
            Error{ErrorCode::ModelLoadFailed,
                  "Failed to import MLX function: " + std::string(error.what())}};
    }
#endif
}

}  // namespace corridorkey::core
