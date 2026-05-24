#pragma once

#include "ofx_constants.hpp"
#include "post_process/screen_color.hpp"

namespace corridorkey::ofx {

inline ScreenColorMode screen_color_mode_from_choice(int screen_color_choice) {
    if (screen_color_choice == kScreenColorBlue) {
        return ScreenColorMode::Blue;
    }
    if (screen_color_choice == kScreenColorBlueGreen) {
        return ScreenColorMode::BlueGreen;
    }
    return ScreenColorMode::Green;
}

}  // namespace corridorkey::ofx
