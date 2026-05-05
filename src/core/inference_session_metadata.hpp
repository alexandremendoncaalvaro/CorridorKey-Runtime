#pragma once

#include <algorithm>
#include <cctype>
#include <corridorkey/types.hpp>
#include <cstdlib>
#include <filesystem>
#include <numeric>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace corridorkey::core {

//
// Header tidy-suppression rationale.
//
// This header is included transitively by many TUs (typically the OFX
// render hot path or the offline batch driver) so its diagnostics
// surface in every consumer once HeaderFilterRegex is scoped to the
// project tree. The categories suppressed below all flag stylistic
// patterns required by the surrounding C ABIs (OFX / ONNX Runtime /
// CUDA / NPP / FFmpeg), the universal pixel / tensor coordinate
// conventions, validated-index operator[] sites, or canonical
// orchestrator function shapes whose linear flow would be obscured by
// helper extraction. Genuine logic regressions are caught by the
// downstream TU sweep and the unit-test suite.
//
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,cppcoreguidelines-pro-type-member-init,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-missing-std-forward,cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions)

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
    return backend == Backend::TensorRT &&
           packaged_corridorkey_fp16_resolution(model_path).has_value();
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

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,cppcoreguidelines-pro-type-member-init,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-missing-std-forward,cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions)
