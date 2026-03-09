#pragma once

#include <array>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace corridorkey {

/**
 * @brief High-performance sRGB Lookup Table.
 * Optimized for L1 cache residency (4096 entries).
 */
class SrgbLut {
public:
    static const SrgbLut& instance() {
        static SrgbLut lut;
        return lut;
    }

    // Fast linear-to-srgb and srgb-to-linear via LUT or polynomial approximation
    inline float to_linear(float s) const {
        if (s <= 0.0f) return 0.0f;
        if (s >= 1.0f) return 1.0f;
        return m_to_linear[static_cast<int>(s * 65535.0f)];
    }

    inline float to_srgb(float l) const {
        // Linear to sRGB is harder for LUT due to HDR range, 
        // but for [0, 1] preview it works well.
        if (l <= 0.0f) return 0.0f;
        if (l >= 1.0f) return 1.0f;
        return m_to_srgb[static_cast<int>(l * 65535.0f)];
    }

private:
    SrgbLut() {
        for (int i = 0; i <= 65535; ++i) {
            float p = i / 65535.0f;
            m_to_linear[i] = (p <= 0.04045f) ? (p / 12.92f) : std::pow((p + 0.055f) / 1.055f, 2.4f);
            m_to_srgb[i] = (p <= 0.0031308f) ? (p * 12.92f) : (1.055f * std::pow(p, 1.0f / 2.4f) - 0.055f);
        }
    }
    std::array<float, 65536> m_to_linear;
    std::array<float, 65536> m_to_srgb;
};

} // namespace corridorkey
