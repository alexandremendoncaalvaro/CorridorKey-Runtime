#pragma once

#include <algorithm>
#include <cctype>
#include <corridorkey/types.hpp>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

namespace corridorkey::core {

inline constexpr std::string_view k_corridorkey_alpha_output_name = "alpha";
inline constexpr std::string_view k_corridorkey_fg_output_name = "fg";

enum class IoBindingMode : std::uint8_t {
    Auto,
    On,
    Off,
};

inline std::optional<int> infer_model_resolution_from_path(
    const std::filesystem::path& model_path) {
    const std::string filename = model_path.filename().string();
    for (const int resolution : {2048, 1536, 1024, 768, 512}) {
        const std::string marker = "_" + std::to_string(resolution);
        if (filename.find(marker) != std::string::npos) {
            return resolution;
        }
    }
    return std::nullopt;
}

inline std::optional<int> packaged_corridorkey_fp16_resolution(
    const std::filesystem::path& model_path) {
    const std::string filename = model_path.filename().string();
    if (!filename.starts_with("corridorkey_fp16_") || !filename.ends_with(".onnx")) {
        return std::nullopt;
    }
    const auto resolution = infer_model_resolution_from_path(model_path);
    if (!resolution.has_value() || *resolution < 1536) {
        return std::nullopt;
    }
    return resolution;
}

inline bool should_use_packaged_corridorkey_output_contract(const std::filesystem::path& model_path,
                                                            Backend backend, bool input_is_fp16) {
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

inline std::optional<IoBindingMode> parse_io_binding_mode(std::string_view raw_mode) {
    std::string normalized(raw_mode);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    if (normalized == "auto") {
        return IoBindingMode::Auto;
    }
    if (normalized == "on" || normalized == "enabled" || normalized == "true" ||
        normalized == "1") {
        return IoBindingMode::On;
    }
    if (normalized == "off" || normalized == "disabled" || normalized == "false" ||
        normalized == "0") {
        return IoBindingMode::Off;
    }
    return std::nullopt;
}

inline std::string_view io_binding_mode_to_string(IoBindingMode mode) {
    switch (mode) {
        case IoBindingMode::On:
            return "on";
        case IoBindingMode::Off:
            return "off";
        case IoBindingMode::Auto:
        default:
            return "auto";
    }
}

inline IoBindingMode io_binding_mode_from_environment() {
    if (const char* raw_mode = std::getenv("CORRIDORKEY_IO_BINDING"); raw_mode != nullptr) {
        if (const auto parsed = parse_io_binding_mode(raw_mode); parsed.has_value()) {
            return *parsed;
        }
    }
    return IoBindingMode::Auto;
}

inline bool supports_windows_rtx_io_binding(const std::filesystem::path& model_path,
                                            Backend backend) {
#if defined(_WIN32)
    return backend == Backend::TensorRT && packaged_corridorkey_fp16_resolution(model_path).has_value();
#else
    (void)model_path;
    (void)backend;
    return false;
#endif
}

inline bool should_enable_io_binding(const std::filesystem::path& model_path, Backend backend,
                                     IoBindingMode mode = io_binding_mode_from_environment()) {
    if (mode == IoBindingMode::Off) {
        return false;
    }
    return supports_windows_rtx_io_binding(model_path, backend);
}

inline std::vector<std::size_t> packaged_corridorkey_output_indices(
    const std::vector<std::string>& output_names) {
    std::vector<std::size_t> indices(output_names.size());
    std::iota(indices.begin(), indices.end(), 0);

    const auto alpha_it =
        std::find(output_names.begin(), output_names.end(), k_corridorkey_alpha_output_name);
    const auto fg_it =
        std::find(output_names.begin(), output_names.end(), k_corridorkey_fg_output_name);
    if (alpha_it == output_names.end() || fg_it == output_names.end()) {
        return indices;
    }

    return {static_cast<std::size_t>(std::distance(output_names.begin(), alpha_it)),
            static_cast<std::size_t>(std::distance(output_names.begin(), fg_it))};
}

inline bool should_allocate_foreground_buffer(bool output_alpha_only,
                                              std::size_t output_tensor_count,
                                              bool bound_fg_available) {
    return !output_alpha_only && (bound_fg_available || output_tensor_count > 1);
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
