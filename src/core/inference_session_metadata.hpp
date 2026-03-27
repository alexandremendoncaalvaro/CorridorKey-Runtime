#pragma once

#include <corridorkey/types.hpp>
#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace corridorkey::core {

inline constexpr std::string_view k_corridorkey_alpha_output_name = "alpha";
inline constexpr std::string_view k_corridorkey_fg_output_name = "fg";

inline std::optional<int> packaged_corridorkey_fp16_resolution(
    const std::filesystem::path& model_path) {
    const std::string filename = model_path.filename().string();
    if (!filename.starts_with("corridorkey_fp16_") || !filename.ends_with(".onnx")) {
        return std::nullopt;
    }
    for (const int resolution : {2048, 1536}) {
        const std::string marker = "_" + std::to_string(resolution);
        if (filename.find(marker) != std::string::npos) {
            return resolution;
        }
    }
    return std::nullopt;
}

inline bool should_use_packaged_corridorkey_output_contract(
    const std::filesystem::path& model_path, Backend backend, bool input_is_fp16) {
#if defined(_WIN32)
    return backend == Backend::TensorRT && input_is_fp16 &&
           packaged_corridorkey_fp16_resolution(model_path).has_value();
#else
    (void)model_path;
    (void)backend;
    (void)input_is_fp16;
    return false;
#endif
}

inline std::optional<int> infer_model_resolution(const std::vector<int64_t>& input_shape) {
    if (input_shape.size() < 4 || input_shape[2] <= 0 || input_shape[3] <= 0) {
        return std::nullopt;
    }
    if (input_shape[2] != input_shape[3]) {
        return std::nullopt;
    }
    return static_cast<int>(input_shape[2]);
}

}  // namespace corridorkey::core
