#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace corridorkey {

inline constexpr std::array<float, 3> kCorridorKeyRgbMean = {0.485F, 0.456F, 0.406F};
inline constexpr std::array<float, 3> kCorridorKeyRgbInvStddev = {
    1.0F / 0.229F,
    1.0F / 0.224F,
    1.0F / 0.225F,
};

enum class ModelRgbChannel : std::uint8_t {
    Red,
    Green,
    Blue,
};

inline float normalize_corridorkey_rgb(float value, ModelRgbChannel channel) {
    switch (channel) {
        case ModelRgbChannel::Red:
            return (value - kCorridorKeyRgbMean[0]) * kCorridorKeyRgbInvStddev[0];
        case ModelRgbChannel::Green:
            return (value - kCorridorKeyRgbMean[1]) * kCorridorKeyRgbInvStddev[1];
        case ModelRgbChannel::Blue:
            return (value - kCorridorKeyRgbMean[2]) * kCorridorKeyRgbInvStddev[2];
    }
    return value;
}

}  // namespace corridorkey
