#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#if defined(_MSC_VER)
#include <immintrin.h>
#else
#include <x86intrin.h>
#endif
#define CORRIDORKEY_HAS_X86_INTRINSICS 1
#endif

namespace corridorkey::common {

inline void convert_fp16_to_fp32(const uint16_t* src, float* dst, std::size_t count) {
    std::size_t index = 0;

#if defined(CORRIDORKEY_HAS_X86_INTRINSICS)
#if defined(__F16C__) || (defined(_MSC_VER) && defined(__AVX2__)) || defined(_MSC_VER)
    for (; index + 8 <= count; index += 8) {
        __m128i half_vec = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + index));
        __m256 float_vec = _mm256_cvtph_ps(half_vec);
        _mm256_storeu_ps(dst + index, float_vec);
    }
#endif
#endif

    for (; index < count; ++index) {
        uint16_t h = src[index];
        uint32_t sign = static_cast<uint32_t>(h >> 15) << 31;
        uint32_t exponent = (h >> 10) & 0x1F;
        uint32_t mantissa = h & 0x3FF;

        uint32_t result = 0;
        if (exponent == 0) {
            if (mantissa != 0) {
                exponent = 127 - 14;
                while ((mantissa & 0x400) == 0) {
                    mantissa <<= 1;
                    --exponent;
                }
                mantissa &= 0x3FF;
                result = sign | (exponent << 23) | (mantissa << 13);
            } else {
                result = sign;
            }
        } else if (exponent == 31) {
            result = sign | 0x7F800000 | (mantissa << 13);
        } else {
            result = sign | ((exponent + 112) << 23) | (mantissa << 13);
        }

        float value = 0.0F;
        static_assert(sizeof(result) == sizeof(value));
        std::memcpy(&value, &result, sizeof(value));
        dst[index] = value;
    }
}

}  // namespace corridorkey::common
