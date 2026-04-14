#pragma once

#include <algorithm>
#include <corridorkey/types.hpp>

#include "common/parallel_for.hpp"
#include "ofx_constants.hpp"

namespace corridorkey::ofx {

enum class ScreenColorMode {
    Green,
    Blue,
};

inline ScreenColorMode screen_color_mode_from_choice(int screen_color_choice) {
    return screen_color_choice == kScreenColorBlue ? ScreenColorMode::Blue
                                                   : ScreenColorMode::Green;
}

inline bool screen_color_requires_green_domain_canonicalization(ScreenColorMode mode) {
    return mode == ScreenColorMode::Blue;
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

inline void canonicalize_to_green_domain(Image image, ScreenColorMode mode) {
    if (!screen_color_requires_green_domain_canonicalization(mode)) {
        return;
    }

    swap_green_blue_channels(image);
}

inline void restore_from_green_domain(Image image, ScreenColorMode mode) {
    canonicalize_to_green_domain(image, mode);
}

}  // namespace corridorkey::ofx
