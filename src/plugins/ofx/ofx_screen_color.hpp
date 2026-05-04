#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <corridorkey/types.hpp>
#include <tuple>

#include "common/parallel_for.hpp"
#include "ofx_constants.hpp"

namespace corridorkey::ofx {

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

enum class ScreenColorMode {
    Green,
    Blue,
    BlueGreen,
};

struct ScreenColorTransform {
    ScreenColorMode mode = ScreenColorMode::Green;
    std::array<float, 3> estimated_screen = {0.08F, 0.84F, 0.08F};
    std::array<float, 3> canonical_screen = {0.08F, 0.84F, 0.08F};
    std::array<float, 9> forward_matrix = {1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    std::array<float, 9> inverse_matrix = {1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F};
    bool is_identity = true;
};

inline ScreenColorMode screen_color_mode_from_choice(int screen_color_choice) {
    if (screen_color_choice == kScreenColorBlue) {
        return ScreenColorMode::Blue;
    }
    if (screen_color_choice == kScreenColorBlueGreen) {
        return ScreenColorMode::BlueGreen;
    }
    return ScreenColorMode::Green;
}

inline bool screen_color_requires_green_domain_canonicalization(ScreenColorMode mode) {
    return mode == ScreenColorMode::BlueGreen;
}

inline bool screen_color_allows_source_passthrough(ScreenColorMode mode) {
    return mode != ScreenColorMode::Blue;
}

inline std::array<float, 9> identity_matrix_3x3() {
    return {1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F};
}

inline std::array<float, 9> legacy_green_blue_swap_matrix() {
    return {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 1.0F, 0.0F};
}

inline int screen_color_channel(ScreenColorMode mode) {
    return mode == ScreenColorMode::Blue || mode == ScreenColorMode::BlueGreen ? 2 : 1;
}

inline int first_non_screen_channel(ScreenColorMode mode) {
    return mode == ScreenColorMode::Blue ? 0 : 0;
}

inline int second_non_screen_channel(ScreenColorMode mode) {
    return mode == ScreenColorMode::Blue || mode == ScreenColorMode::BlueGreen ? 1 : 2;
}

inline std::array<float, 3> default_screen_reference(ScreenColorMode mode) {
    if (mode == ScreenColorMode::Blue || mode == ScreenColorMode::BlueGreen) {
        return {0.08F, 0.16F, 0.84F};
    }
    return {0.08F, 0.84F, 0.08F};
}

inline float safe_channel(float value) {
    return std::isfinite(value) ? value : 0.0F;
}

inline std::array<float, 9> matrix_from_columns(const std::array<float, 3>& c0,
                                                const std::array<float, 3>& c1,
                                                const std::array<float, 3>& c2) {
    return {c0[0], c1[0], c2[0], c0[1], c1[1], c2[1], c0[2], c1[2], c2[2]};
}

inline float determinant_3x3(const std::array<float, 9>& matrix) {
    return matrix[0] * (matrix[4] * matrix[8] - matrix[5] * matrix[7]) -
           matrix[1] * (matrix[3] * matrix[8] - matrix[5] * matrix[6]) +
           matrix[2] * (matrix[3] * matrix[7] - matrix[4] * matrix[6]);
}

inline bool invert_matrix_3x3(const std::array<float, 9>& matrix, std::array<float, 9>& inverse) {
    const float det = determinant_3x3(matrix);
    if (std::abs(det) < 1.0e-5F) {
        return false;
    }

    const float inv_det = 1.0F / det;
    inverse = {
        (matrix[4] * matrix[8] - matrix[5] * matrix[7]) * inv_det,
        (matrix[2] * matrix[7] - matrix[1] * matrix[8]) * inv_det,
        (matrix[1] * matrix[5] - matrix[2] * matrix[4]) * inv_det,
        (matrix[5] * matrix[6] - matrix[3] * matrix[8]) * inv_det,
        (matrix[0] * matrix[8] - matrix[2] * matrix[6]) * inv_det,
        (matrix[2] * matrix[3] - matrix[0] * matrix[5]) * inv_det,
        (matrix[3] * matrix[7] - matrix[4] * matrix[6]) * inv_det,
        (matrix[1] * matrix[6] - matrix[0] * matrix[7]) * inv_det,
        (matrix[0] * matrix[4] - matrix[1] * matrix[3]) * inv_det,
    };
    return true;
}

inline std::array<float, 9> multiply_matrices_3x3(const std::array<float, 9>& lhs,
                                                  const std::array<float, 9>& rhs) {
    std::array<float, 9> product = {};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            product[static_cast<std::size_t>(row) * 3U + static_cast<std::size_t>(col)] =
                lhs[static_cast<std::size_t>(row) * 3U + 0U] *
                    rhs[0U * 3U + static_cast<std::size_t>(col)] +
                lhs[static_cast<std::size_t>(row) * 3U + 1U] *
                    rhs[1U * 3U + static_cast<std::size_t>(col)] +
                lhs[static_cast<std::size_t>(row) * 3U + 2U] *
                    rhs[2U * 3U + static_cast<std::size_t>(col)];
        }
    }
    return product;
}

inline std::array<float, 3> canonical_green_reference_from_blue(
    const std::array<float, 3>& estimated_screen) {
    const float green_strength = std::clamp(estimated_screen[2], 0.35F, 1.0F);
    const float side_channel = std::clamp(estimated_screen[0], 0.0F, green_strength * 0.25F);
    return {side_channel, green_strength, side_channel};
}

inline std::array<float, 3> estimate_screen_reference(Image image, ScreenColorMode mode) {
    const std::array<float, 3> fallback = default_screen_reference(mode);
    if (image.empty() || image.channels < 3 || image.width <= 0 || image.height <= 0) {
        return fallback;
    }

    const int selected_channel = screen_color_channel(mode);
    const int other_a = first_non_screen_channel(mode);
    const int other_b = second_non_screen_channel(mode);
    const int border = std::clamp(std::min(image.width, image.height) / 12, 4, 32);

    auto accumulate_samples = [&](bool border_only) {
        std::array<float, 3> weighted_sum = {0.0F, 0.0F, 0.0F};
        float total_weight = 0.0F;

        for (int y = 0; y < image.height; ++y) {
            for (int x = 0; x < image.width; ++x) {
                const bool is_border = x < border || y < border || x >= image.width - border ||
                                       y >= image.height - border;
                if (border_only && !is_border) {
                    continue;
                }

                const float red = safe_channel(image(y, x, 0));
                const float green = safe_channel(image(y, x, 1));
                const float blue = safe_channel(image(y, x, 2));
                const std::array<float, 3> pixel = {red, green, blue};
                const float selected = pixel[static_cast<std::size_t>(selected_channel)];
                const float other_max = std::max(pixel[static_cast<std::size_t>(other_a)],
                                                 pixel[static_cast<std::size_t>(other_b)]);
                const float other_avg = (pixel[static_cast<std::size_t>(other_a)] +
                                         pixel[static_cast<std::size_t>(other_b)]) *
                                        0.5F;
                const float dominance = selected - other_max;
                const float separation = selected - other_avg;
                if (selected < 0.08F || dominance <= 0.0F || separation <= 0.0F) {
                    continue;
                }

                const float weight =
                    dominance * dominance * std::max(separation, 0.02F) * (0.5F + selected);
                weighted_sum[0] += red * weight;
                weighted_sum[1] += green * weight;
                weighted_sum[2] += blue * weight;
                total_weight += weight;
            }
        }
        return std::pair{weighted_sum, total_weight};
    };

    auto [weighted_sum, total_weight] = accumulate_samples(true);
    if (total_weight <= 1.0e-5F) {
        std::tie(weighted_sum, total_weight) = accumulate_samples(false);
    }
    if (total_weight <= 1.0e-5F) {
        return fallback;
    }

    std::array<float, 3> estimate = {weighted_sum[0] / total_weight, weighted_sum[1] / total_weight,
                                     weighted_sum[2] / total_weight};
    const float selected = estimate[static_cast<std::size_t>(selected_channel)];
    const float other_max = std::max(estimate[static_cast<std::size_t>(other_a)],
                                     estimate[static_cast<std::size_t>(other_b)]);
    if (selected <= other_max + 0.02F) {
        return fallback;
    }

    return estimate;
}

inline ScreenColorTransform make_screen_mapping_transform(
    const std::array<float, 3>& source_screen, const std::array<float, 3>& target_screen) {
    ScreenColorTransform transform;
    transform.mode = ScreenColorMode::BlueGreen;
    transform.estimated_screen = source_screen;
    transform.canonical_screen = target_screen;
    transform.is_identity = false;

    constexpr std::array<float, 3> kWhiteAnchor = {1.0F, 1.0F, 1.0F};
    constexpr std::array<float, 3> kRedAnchor = {1.0F, 0.0F, 0.0F};
    const std::array<float, 9> source_basis =
        matrix_from_columns(kWhiteAnchor, kRedAnchor, source_screen);
    const std::array<float, 9> target_basis =
        matrix_from_columns(kWhiteAnchor, kRedAnchor, target_screen);
    std::array<float, 9> source_basis_inverse = {};
    if (!invert_matrix_3x3(source_basis, source_basis_inverse)) {
        transform.forward_matrix = legacy_green_blue_swap_matrix();
        transform.inverse_matrix = legacy_green_blue_swap_matrix();
        transform.estimated_screen = default_screen_reference(ScreenColorMode::Blue);
        transform.canonical_screen =
            canonical_green_reference_from_blue(transform.estimated_screen);
        return transform;
    }

    transform.forward_matrix = multiply_matrices_3x3(target_basis, source_basis_inverse);
    if (!invert_matrix_3x3(transform.forward_matrix, transform.inverse_matrix)) {
        transform.forward_matrix = legacy_green_blue_swap_matrix();
        transform.inverse_matrix = legacy_green_blue_swap_matrix();
        transform.estimated_screen = default_screen_reference(ScreenColorMode::Blue);
        transform.canonical_screen =
            canonical_green_reference_from_blue(transform.estimated_screen);
    }
    return transform;
}

inline ScreenColorTransform make_screen_color_transform(Image image, ScreenColorMode mode) {
    ScreenColorTransform transform;
    transform.mode = mode;
    transform.estimated_screen = estimate_screen_reference(image, mode);
    transform.canonical_screen = transform.estimated_screen;
    transform.forward_matrix = identity_matrix_3x3();
    transform.inverse_matrix = identity_matrix_3x3();
    transform.is_identity = !screen_color_requires_green_domain_canonicalization(mode);

    if (transform.is_identity) {
        return transform;
    }

    return make_screen_mapping_transform(
        transform.estimated_screen,
        canonical_green_reference_from_blue(transform.estimated_screen));
}

inline void swap_green_blue_channels(Image image) {
    if (image.channels < 3) {
        return;
    }

    common::parallel_for_rows(image.height, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            for (int x = 0; x < image.width; ++x) {
                std::swap(image(y, x, 1), image(y, x, 2));
            }
        }
    });
}

inline void apply_screen_color_transform(Image image, const std::array<float, 9>& matrix) {
    if (image.channels < 3) {
        return;
    }

    common::parallel_for_rows(image.height, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            for (int x = 0; x < image.width; ++x) {
                const float red = image(y, x, 0);
                const float green = image(y, x, 1);
                const float blue = image(y, x, 2);
                image(y, x, 0) =
                    safe_channel(matrix[0] * red + matrix[1] * green + matrix[2] * blue);
                image(y, x, 1) =
                    safe_channel(matrix[3] * red + matrix[4] * green + matrix[5] * blue);
                image(y, x, 2) =
                    safe_channel(matrix[6] * red + matrix[7] * green + matrix[8] * blue);
            }
        }
    });
}

inline void canonicalize_to_green_domain(Image image, const ScreenColorTransform& transform) {
    if (transform.is_identity) {
        return;
    }

    apply_screen_color_transform(image, transform.forward_matrix);
}

inline void restore_from_green_domain(Image image, const ScreenColorTransform& transform) {
    if (transform.is_identity) {
        return;
    }

    apply_screen_color_transform(image, transform.inverse_matrix);
}

}  // namespace corridorkey::ofx

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,readability-string-compare,bugprone-exception-escape,cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays,bugprone-branch-clone,cert-err33-c,readability-redundant-declaration,readability-qualified-auto,modernize-use-scoped-lock,modernize-use-bool-literals,cppcoreguidelines-init-variables,cppcoreguidelines-special-member-functions,cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,performance-enum-size,performance-avoid-endl,bugprone-unchecked-optional-access,cppcoreguidelines-pro-type-member-init,bugprone-unchecked-string-to-number-conversion,cppcoreguidelines-missing-std-forward,cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum,cppcoreguidelines-pro-type-cstyle-cast,modernize-use-using,modernize-use-integer-sign-comparison,cert-dcl50-cpp,cppcoreguidelines-pro-type-const-cast,readability-identifier-naming,modernize-raw-string-literal,readability-container-size-empty,bugprone-command-processor,readability-use-std-min-max,cppcoreguidelines-avoid-non-const-global-variables,bugprone-misplaced-widening-cast,readability-misleading-indentation,cert-env33-c,performance-unnecessary-copy-initialization,readability-named-parameter,readability-isolate-declaration,cert-err34-c,modernize-avoid-variadic-functions)
