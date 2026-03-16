#pragma once

#include <corridorkey/types.hpp>

namespace corridorkey {

/**
 * @brief Restore original source detail in confidently opaque interior regions.
 *
 * The ML model output loses fine texture in fully opaque areas. This function
 * blends original source pixels back where the alpha matte is confidently opaque
 * and far from any alpha edge, avoiding green-spill contamination.
 *
 * @param foreground  3-channel model foreground (sRGB), modified in-place
 * @param source      3-channel original source image (sRGB)
 * @param alpha       1-channel alpha matte
 * @param alpha_threshold  Minimum alpha to consider "opaque" (default 0.92)
 * @param distance_threshold  Minimum distance from alpha edge in pixels (default 8)
 */
void restore_source_detail(Image foreground, const Image& source, const Image& alpha,
                           float alpha_threshold = 0.92f, int distance_threshold = 8);

}  // namespace corridorkey
