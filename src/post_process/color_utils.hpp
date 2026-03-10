#pragma once

#include <corridorkey/types.hpp>

namespace corridorkey {

/**
 * @brief Pure math functions for pixel data.
 * All functions work on raw float buffers in linear space.
 */
class ColorUtils {
   public:
    static void srgb_to_linear(Image image);
    static void linear_to_srgb(Image image);

    static void premultiply(Image rgb, const Image alpha);
    static void unpremultiply(Image rgb, const Image alpha);

    static void composite_over_checker(Image rgba);

    /**
     * @brief Generate a rough alpha matte using a basic green-screen threshold.
     * Used as a fallback "hint" if no manual alpha hint is provided.
     */
    static void generate_rough_matte(const Image rgb, Image alpha_hint);

    /**
     * @brief Resize an image using bilinear interpolation.
     * Returns an owned ImageBuffer.
     */
    static ImageBuffer resize(const Image image, int new_width, int new_height);

    /**
     * @brief Resize image maintaining aspect ratio and padding to reach target size (Letterbox).
     * Returns the padded image and the original valid area (ROI).
     */
    static std::pair<ImageBuffer, struct Rect> fit_pad(const Image image, int target_w,
                                                       int target_h);

    /**
     * @brief Crop an image using a defined rectangle.
     */
    static ImageBuffer crop(const Image image, int x, int y, int w, int h);

    /**
     * @brief Convert interleaved HWC data to planar NCHW data.
     * Uses 64x64 tiling for cache efficiency.
     */
    static void to_planar(const Image src, float* dst);

    /**
     * @brief Convert planar NCHW data back to interleaved HWC data.
     * Uses 64x64 tiling for cache efficiency.
     */
    static void from_planar(const float* src, Image dst);
};

}  // namespace corridorkey
