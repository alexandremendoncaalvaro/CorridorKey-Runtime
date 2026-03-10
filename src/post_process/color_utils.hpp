#pragma once

#include <corridorkey/types.hpp>
#include <utility>

namespace corridorkey {

/**
 * @brief High-performance pixel manipulation utilities.
 * All functions work on Linear Float data unless specified.
 */
class ColorUtils {
   public:
    /**
     * @brief Apply premultiplication: RGB * Alpha.
     */
    static void premultiply(Image rgb, Image alpha);

    /**
     * @brief Reverse premultiplication: RGB / Alpha.
     */
    static void unpremultiply(Image rgb, Image alpha);

    /**
     * @brief Composite an image over a standard VFX checkerboard.
     * Useful for preview generation.
     */
    static void composite_over_checker(Image rgba);

    /**
     * @brief Generate a rough alpha guide using a simple green-key fallback.
     * Used when no manual alpha hint is provided.
     */
    static void generate_rough_matte(Image rgb, Image alpha_hint);

    /**
     * @brief Resize an image using bilinear interpolation.
     * Returns an owned ImageBuffer.
     */
    static ImageBuffer resize(Image image, int new_width, int new_height);

    /**
     * @brief Resize image maintaining aspect ratio and padding to reach target size (Letterbox).
     * Returns the padded image and the original valid area (ROI).
     */
    static std::pair<ImageBuffer, Rect> fit_pad(Image image, int target_width, int target_height);

    /**
     * @brief Crop an image using a defined rectangle.
     */
    static ImageBuffer crop(Image image, int x_start, int y_start, int width, int height);

    /**
     * @brief Convert interleaved HWC data to planar NCHW data.
     * Uses 64x64 tiling for cache efficiency.
     */
    static void to_planar(Image src, float* dst);

    /**
     * @brief Convert planar NCHW data to interleaved HWC data.
     */
    static void from_planar(const float* src, Image dst);

    /**
     * @brief Convert sRGB 0-1 float to Linear 0-1 float.
     */
    static void srgb_to_linear(Image image);

    /**
     * @brief Convert Linear 0-1 float to sRGB 0-1 float.
     */
    static void linear_to_srgb(Image image);
};

}  // namespace corridorkey
