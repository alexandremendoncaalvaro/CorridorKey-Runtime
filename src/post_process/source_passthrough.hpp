#pragma once

#include <corridorkey/types.hpp>

#include "post_process/color_utils.hpp"

namespace corridorkey {

/// Blend original source pixels into high-confidence regions of the model foreground.
/// Both source_rgb and model_fg must be in sRGB space, 3-channel, same dimensions.
/// Alpha is single-channel at the same resolution.
/// erode_px: erosion radius for the binary interior mask (elliptical structuring element)
/// blur_px: blur radius for transition smoothing
void source_passthrough(Image source_rgb, Image model_fg, Image alpha, int erode_px, int blur_px,
                        ColorUtils::State& state);

}  // namespace corridorkey
