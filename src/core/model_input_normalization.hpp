#pragma once

#include <array>
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
            return (value - std::get<0>(kCorridorKeyRgbMean)) *
                   std::get<0>(kCorridorKeyRgbInvStddev);
        case ModelRgbChannel::Green:
            return (value - std::get<1>(kCorridorKeyRgbMean)) *
                   std::get<1>(kCorridorKeyRgbInvStddev);
        case ModelRgbChannel::Blue:
            return (value - std::get<2>(kCorridorKeyRgbMean)) *
                   std::get<2>(kCorridorKeyRgbInvStddev);
    }
    return value;
}

}  // namespace corridorkey
