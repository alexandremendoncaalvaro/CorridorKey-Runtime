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
 * @brief Vectorized multiply (dst = src1 * src2)
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

}  // namespace corridorkey::common
