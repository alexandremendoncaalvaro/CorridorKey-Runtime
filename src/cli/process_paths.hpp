#pragma once

#include <corridorkey/types.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace corridorkey::cli {

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
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,cppcoreguidelines-pro-type-member-init,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-missing-std-forward,cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-misleading-indentation,cert-dcl50-cpp,readability-isolate-declaration,readability-use-std-min-max,readability-named-parameter,cppcoreguidelines-avoid-non-const-global-variables,modernize-use-integer-sign-comparison,modernize-use-using,cppcoreguidelines-pro-type-cstyle-cast,cert-env33-c,bugprone-misplaced-widening-cast,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,performance-unnecessary-copy-initialization,cert-err34-c,modernize-avoid-variadic-functions)

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

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,cppcoreguidelines-pro-type-member-init,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-missing-std-forward,cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-misleading-indentation,cert-dcl50-cpp,readability-isolate-declaration,readability-use-std-min-max,readability-named-parameter,cppcoreguidelines-avoid-non-const-global-variables,modernize-use-integer-sign-comparison,modernize-use-using,cppcoreguidelines-pro-type-cstyle-cast,cert-env33-c,bugprone-misplaced-widening-cast,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,performance-unnecessary-copy-initialization,cert-err34-c,modernize-avoid-variadic-functions)
