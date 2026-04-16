#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__APPLE__)
#include <Accelerate/Accelerate.h>
#else
#include <algorithm>
#endif

namespace corridorkey::common {

/**
 * @brief Vectorized clamp (clip) using vDSP if available.
 */
inline void accelerate_vclip(const float* src, std::ptrdiff_t src_stride, const float* low,
                             const float* high, float* dst, std::ptrdiff_t dst_stride,
                             std::size_t count) {
#if defined(__APPLE__)
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
#if defined(__APPLE__)
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
#if defined(__APPLE__)
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
