#pragma once

#include <array>

#include <corridorkey/api_export.hpp>
#include <corridorkey/types.hpp>

namespace corridorkey {

/**
 * @brief High-performance pixel manipulation utilities.
 * All functions work on Linear Float data unless specified.
 */
class CORRIDORKEY_API ColorUtils {
   public:
    struct State {
        struct Offset2D {
            int dy = 0;
            int dx = 0;
        };

        std::vector<float> blur_temp;
        std::vector<float> blur_weights;
        std::vector<float> resize_temp;
        std::vector<float> resize_lanczos_h;
        std::vector<float> sp_mask;
        std::vector<float> sp_temp;
        std::vector<Offset2D> sp_offsets;
        int sp_offsets_radius = -1;
    };

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
    static void composite_premultiplied_over_checker_to_srgb(Image premultiplied_rgba, Image dst);

    /**
     * @brief Generate a rough alpha guide using a simple green-key fallback.
     * Used when no manual alpha hint is provided.
     */
    static void generate_rough_matte(Image rgb, Image alpha_hint);

    /**
     * @brief Resize an image using bilinear interpolation.
     * Returns an owned
     * ImageBuffer.
     */
    static ImageBuffer resize(Image image, int new_width, int new_height);
    static void resize_into(Image image, Image dst);
    static void resize_from_planar_into(const float* src, int src_width, int src_height,
                                        int src_channels, Image dst);
    static void resize_alpha_fg_from_planar_into(const float* alpha_src, const float* fg_src,
                                                 int src_width, int src_height, Image alpha_dst,
                                                 Image fg_dst);

    /**
     * @brief Resize with Gaussian pre-filter for anti-aliased downscaling.
     * When downscaling by more than 1.5x, applies a Gaussian blur before
     * bilinear interpolation to prevent aliasing. For upscaling or small
     * downscale ratios, behaves identically to resize().
     */
    static ImageBuffer resize_area(Image image, int new_width, int new_height, State& state);
    static void resize_area_into(Image image, Image dst, State& state);

    /**
     * @brief Resize an image using Lanczos4 interpolation.
     * Uses BORDER_REFLECT_101 boundary handling matching OpenCV.
     * Higher quality than bilinear for upscaling. Returns an owned ImageBuffer.
     */
    static ImageBuffer resize_lanczos(Image image, int new_width, int new_height, State& state);
    static void resize_lanczos_into(Image image, Image dst, State& state);
    static void resize_lanczos_from_planar_into(const float* src, int src_width, int src_height,
                                                int src_channels, Image dst, State& state);

    /**
     * @brief Apply separable Gaussian blur in-place.
     * @param image Single-channel or multi-channel image to blur.
     * @param sigma Standard deviation of the Gaussian kernel.
     */
    static void gaussian_blur(Image image, float sigma, State& state);

    /**
     * @brief Clamp all pixel values to [min_val, max_val].
     */
    static void clamp_image(Image image, float min_val, float max_val);

    /**
     * @brief Convert interleaved HWC data to planar NCHW data.
     * Uses 64x64 tiling for cache efficiency.
     */
    static void to_planar(Image src, float* dst);
    static void pack_normalized_rgb_and_hint_to_planar(
        Image rgb, Image hint, float* dst, const std::array<float, 3>& mean,
        const std::array<float, 3>& inv_stddev);

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
