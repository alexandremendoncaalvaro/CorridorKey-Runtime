#pragma once

#include <corridorkey/types.hpp>

namespace corridorkey {

/**
 * @brief Luminance-preserving green spill removal.
 * Reduces green channel to avg(R,B), redistributing excess 50/50 to R and B.
 * Operates in sRGB space (matching original Python pipeline).
 * @param rgb RGB image to despill in-place
 * @param strength 0.0 (no effect) to 1.0 (full despill), blended via linear interpolation
 */
void despill(Image rgb, float strength);

}  // namespace corridorkey
