#pragma once

#include <cstddef>
#include <cstdint>

#ifdef __APPLE__
#include <Accelerate/Accelerate.h>
#else
#include <algorithm>
#endif

namespace corridorkey::common {

//
// Header tidy-suppression rationale (file-level NOLINT block).
// Bridges Apple's Accelerate vDSP intrinsics and the portable
// fallback path; vDSP signatures take (src, dst) ordered argument
// pairs that fire bugprone-easily-swappable-parameters but match
// the canonical Apple API exactly. Pixel-stride offsets follow the
// 'base + n * stride' Apple convention.
//
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,bugprone-exception-escape,cert-err33-c,cert-err34-c,modernize-avoid-variadic-functions)

/**
 * @brief Vectorized clamp (clip) using vDSP if available.
 */
inline void accelerate_vclip(const float* src, std::ptrdiff_t src_stride, const float* low,
                             const float* high, float* dst, std::ptrdiff_t dst_stride,
                             std::size_t count) {
#ifdef __APPLE__
    vDSP_vclip(src, src_stride, low, high, dst, dst_stride, count);
#else
    for (std::size_t i = 0; i < count; ++i) {
        dst[i * dst_stride] = std::clamp(src[i * src_stride], *low, *high);
    }
#endif
}

/**
 * @brief Vectorized multiplication: dst = src1 * src2.
 */
inline void accelerate_vmul(const float* src1, std::ptrdiff_t src1_stride, const float* src2,
                            std::ptrdiff_t src2_stride, float* dst, std::ptrdiff_t dst_stride,
                            std::size_t count) {
#ifdef __APPLE__
    vDSP_vmul(src1, src1_stride, src2, src2_stride, dst, dst_stride, count);
#else
    for (std::size_t i = 0; i < count; ++i) {
        dst[i * dst_stride] = src1[i * src1_stride] * src2[i * src2_stride];
    }
#endif
}

/**
 * @brief Vectorized normalization and packing for 4-channel input (RGB + Hint)
 * Used to prepare MLX/Inference inputs bypassing slow manual loops.
 */
inline void accelerate_normalize_and_pack_4ch(const float* rgb, int rgb_stride, const float* hint,
                                              int hint_stride, float* dst, int count,
                                              const float* means, const float* inv_stddevs) {
#ifdef __APPLE__
    // Process R, G, B channels using vDSP_vsmsa (vector-scalar multiply, vector-scalar add)
    // Formula: (val - mean) * inv_stddev => val * inv_stddev + (-mean * inv_stddev)
    for (int c = 0; c < 3; ++c) {
        float s = inv_stddevs[c];
        float b = -means[c] * s;
        vDSP_vsmsa(rgb + c, rgb_stride, &s, &b, dst + c, 4, count);
    }
    // Copy hint channel (copying from source row-major to interleaved destination)
    // Note: vDSP_mmov is for 2D matrices, for 1D stride copy we can use vDSP_vadd with 0
    float zero = 0.0f;
    vDSP_vsadd(hint, hint_stride, &zero, dst + 3, 4, count);
#else
    for (int i = 0; i < count; ++i) {
        dst[i * 4 + 0] = (rgb[i * rgb_stride + 0] - means[0]) * inv_stddevs[0];
        dst[i * 4 + 1] = (rgb[i * rgb_stride + 1] - means[1]) * inv_stddevs[1];
        dst[i * 4 + 2] = (rgb[i * rgb_stride + 2] - means[2]) * inv_stddevs[2];
        dst[i * 4 + 3] = hint[i * hint_stride];
    }
#endif
}

}  // namespace corridorkey::common

// NOLINTEND(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access,cppcoreguidelines-pro-bounds-constant-array-index,readability-identifier-length,bugprone-easily-swappable-parameters,readability-function-cognitive-complexity,readability-function-size,cppcoreguidelines-avoid-magic-numbers,modernize-use-designated-initializers,readability-uppercase-literal-suffix,readability-math-missing-parentheses,modernize-use-ranges,modernize-use-starts-ends-with,modernize-use-emplace,modernize-use-auto,modernize-loop-convert,modernize-avoid-c-style-cast,modernize-return-braced-init-list,readability-implicit-bool-conversion,readability-container-contains,readability-redundant-member-init,readability-redundant-string-init,bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions,readability-avoid-nested-conditional-operator,modernize-use-nodiscard,readability-make-member-function-const,cppcoreguidelines-pro-type-reinterpret-cast,bugprone-implicit-widening-of-multiplication-result,readability-redundant-inline-specifier,cppcoreguidelines-prefer-member-initializer,performance-unnecessary-value-param,readability-use-concise-preprocessor-directives,readability-else-after-return,bugprone-exception-escape,cert-err33-c,cert-err34-c,modernize-avoid-variadic-functions)
