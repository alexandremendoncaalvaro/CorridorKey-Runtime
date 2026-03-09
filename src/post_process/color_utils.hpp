#pragma once

#include <corridorkey/types.hpp>

namespace corridorkey {

/**
 * @brief Pure math functions for pixel data.
 * All functions work on raw float buffers in linear space.
 */
class ColorUtils {
public:
    static void srgb_to_linear(Image& image);
    static void linear_to_srgb(Image& image);
    
    static void premultiply(Image& rgb, const Image& alpha);
    static void unpremultiply(Image& rgb, const Image& alpha);
    
    static void despill(Image& rgb, const Image& alpha, float strength);
    
    static void despeckle(Image& alpha, int size_threshold);
    
    static void composite_over_checker(Image& rgba);
    
    static Image resize(const Image& image, int new_width, int new_height);
};

} // namespace corridorkey
