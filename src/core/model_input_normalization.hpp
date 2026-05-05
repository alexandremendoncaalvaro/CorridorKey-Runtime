#pragma once

#include <array>
#include <cstddef>

namespace corridorkey {

inline constexpr std::array<float, 3> kCorridorKeyRgbMean = {0.485F, 0.456F, 0.406F};
inline constexpr std::array<float, 3> kCorridorKeyRgbInvStddev = {
    1.0F / 0.229F,
    1.0F / 0.224F,
    1.0F / 0.225F,
};

inline float normalize_corridorkey_rgb(float value, int channel) {
    const auto index = static_cast<std::size_t>(channel);
    return (value - kCorridorKeyRgbMean[index]) * kCorridorKeyRgbInvStddev[index];
}

}  // namespace corridorkey
