#pragma once

#include <corridorkey/types.hpp>

namespace corridorkey {

/**
 * @brief Connected-component based alpha matte cleanup.
 * Matches original Python clean_matte algorithm:
 *   1. Threshold alpha at 0.5 to binary mask
 *   2. Find 8-connected components, remove those below area_threshold
 *   3. Dilate with elliptical kernel (radius = dilation)
 *   4. Gaussian blur for smooth edges
 *   5. Multiply original alpha by cleaned mask
 *
 * @param alpha    Single-channel alpha image to clean in-place
 * @param area_threshold  Minimum component area in pixels to keep (default 400)
 * @param dilation        Elliptical dilation radius (default 25, kernel = 2*dilation+1)
 * @param blur_size       Gaussian blur half-size (default 5, kernel = 2*blur_size+1)
 */
void despeckle(Image alpha, int area_threshold, int dilation = 25, int blur_size = 5);

} // namespace corridorkey
