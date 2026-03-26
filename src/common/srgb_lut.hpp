#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace corridorkey {

/**
 * @brief High-performance sRGB Lookup Table.
 * 65536 entries (512KB total) for 16-bit precision. Fits in L2/L3 cache on modern CPUs.
 *
 * This singleton is a justified exception to the "no global state" rule:
 * the LUT is immutable after construction, thread-safe, and const-only.
 * Passing it by parameter would degrade the API without benefit.
 */
class SrgbLut {
   public:
    static const SrgbLut& instance() {
        static SrgbLut lut;
        return lut;
    }

    inline float to_linear(float s) const {
        return m_to_linear[unit_interval_index(s)];
    }

    inline float to_srgb(float l) const {
        return m_to_srgb[unit_interval_index(l)];
    }

   private:
    static std::size_t unit_interval_index(float value) {
        if (std::isnan(value) || value <= 0.0f) {
            return 0;
        }
        if (!std::isfinite(value)) {
            return 65535;
        }
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        return static_cast<std::size_t>(clamped * 65535.0f);
    }

    SrgbLut() {
        for (int i = 0; i <= 65535; ++i) {
            float p = i / 65535.0f;
            m_to_linear[i] = (p <= 0.04045f) ? (p / 12.92f) : std::pow((p + 0.055f) / 1.055f, 2.4f);
            m_to_srgb[i] =
                (p <= 0.0031308f) ? (p * 12.92f) : (1.055f * std::pow(p, 1.0f / 2.4f) - 0.055f);
        }
    }
    std::array<float, 65536> m_to_linear;
    std::array<float, 65536> m_to_srgb;
};

}  // namespace corridorkey
