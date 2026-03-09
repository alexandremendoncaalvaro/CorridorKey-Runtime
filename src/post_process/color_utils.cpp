#include "color_utils.hpp"
#include "common/srgb_lut.hpp"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <vector>

#if __has_include(<execution>) && (defined(__cpp_lib_execution) || !defined(__clang__))
#include <execution>
#define EXEC_POLICY std::execution::par_unseq ,
#else
#define EXEC_POLICY
#endif

namespace corridorkey {

void ColorUtils::srgb_to_linear(Image image) {
    const auto& lut = SrgbLut::instance();
    std::for_each(EXEC_POLICY image.data.begin(), image.data.end(), [&](float& p) {
        p = lut.to_linear(p);
    });
}

void ColorUtils::linear_to_srgb(Image image) {
    const auto& lut = SrgbLut::instance();
    std::for_each(EXEC_POLICY image.data.begin(), image.data.end(), [&](float& p) {
        p = lut.to_srgb(p);
    });
}

void ColorUtils::premultiply(Image rgb, const Image alpha) {
    if (rgb.empty() || alpha.empty()) return;

    size_t pixels = static_cast<size_t>(rgb.width) * rgb.height;
    int channels = rgb.channels;
    std::vector<int> indices(pixels);
    std::iota(indices.begin(), indices.end(), 0);

    std::for_each(EXEC_POLICY indices.begin(), indices.end(), [&](int i) {
        float a = alpha.data[i];
        float* p = &rgb.data[i * channels];
        p[0] *= a;
        p[1] *= a;
        p[2] *= a;
    });
}

void ColorUtils::unpremultiply(Image rgb, const Image alpha) {
    if (rgb.empty() || alpha.empty()) return;

    size_t pixels = static_cast<size_t>(rgb.width) * rgb.height;
    int channels = rgb.channels;
    std::vector<int> indices(pixels);
    std::iota(indices.begin(), indices.end(), 0);

    std::for_each(EXEC_POLICY indices.begin(), indices.end(), [&](int i) {
        float a = alpha.data[i];
        if (a > 0.0001f) {
            float* p = &rgb.data[i * channels];
            float inv_a = 1.0f / a;
            p[0] *= inv_a;
            p[1] *= inv_a;
            p[2] *= inv_a;
        }
    });
}

// Tiled HWC -> NCHW conversion for cache efficiency
void ColorUtils::to_planar(const Image src, float* dst) {
    int w = src.width;
    int h = src.height;
    int c = src.channels;

    constexpr int TILE = 64;
    for (int ty = 0; ty < h; ty += TILE) {
        for (int tx = 0; tx < w; tx += TILE) {
            int tile_h = std::min(TILE, h - ty);
            int tile_w = std::min(TILE, w - tx);
            for (int ch = 0; ch < c; ++ch) {
                float* plane = dst + (static_cast<size_t>(ch) * w * h);
                for (int y = ty; y < ty + tile_h; ++y) {
                    for (int x = tx; x < tx + tile_w; ++x) {
                        plane[y * w + x] = src.data[(static_cast<size_t>(y) * w + x) * c + ch];
                    }
                }
            }
        }
    }
}

// Tiled NCHW -> HWC conversion for cache efficiency
void ColorUtils::from_planar(const float* src, Image dst) {
    int w = dst.width;
    int h = dst.height;
    int c = dst.channels;

    constexpr int TILE = 64;
    for (int ty = 0; ty < h; ty += TILE) {
        for (int tx = 0; tx < w; tx += TILE) {
            int tile_h = std::min(TILE, h - ty);
            int tile_w = std::min(TILE, w - tx);
            for (int ch = 0; ch < c; ++ch) {
                const float* plane = src + (static_cast<size_t>(ch) * w * h);
                for (int y = ty; y < ty + tile_h; ++y) {
                    for (int x = tx; x < tx + tile_w; ++x) {
                        dst.data[(static_cast<size_t>(y) * w + x) * c + ch] = plane[y * w + x];
                    }
                }
            }
        }
    }
}

void ColorUtils::composite_over_checker(Image rgba) {
    if (rgba.empty() || rgba.channels < 4) return;

    // Checker params matching original Python: sRGB 0.15/0.55, checker_size=128
    // Convert sRGB constants to linear (matching cu.srgb_to_linear(bg_srgb))
    const auto& lut = SrgbLut::instance();
    const float bg_dark = lut.to_linear(0.15f);
    const float bg_light = lut.to_linear(0.55f);

    int h = rgba.height;
    int w = rgba.width;
    std::vector<int> rows(h);
    std::iota(rows.begin(), rows.end(), 0);

    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < w; ++x) {
            float alpha = std::clamp(rgba(y, x, 3), 0.0f, 1.0f);
            float bg = (((x / 128) + (y / 128)) % 2 == 0) ? bg_dark : bg_light;
            for (int c = 0; c < 3; ++c) {
                rgba(y, x, c) = rgba(y, x, c) + bg * (1.0f - alpha);
            }
            rgba(y, x, 3) = 1.0f;
        }
    });
}

void ColorUtils::generate_rough_matte(const Image rgb, Image alpha_hint) {
    if (rgb.empty() || alpha_hint.empty()) return;

    int h = rgb.height;
    int w = rgb.width;
    std::vector<int> rows(h);
    std::iota(rows.begin(), rows.end(), 0);

    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < w; ++x) {
            float r = rgb(y, x, 0);
            float g = rgb(y, x, 1);
            float b = rgb(y, x, 2);

            // Simple green screen threshold: g > r and g > b
            // We look for pixels where green is significantly higher than others
            float max_rb = std::max(r, b);
            float green_diff = g - max_rb;
            
            // Map difference to a rough alpha: 
            // If green is much higher, alpha goes to 0 (transparent)
            // If not green, alpha stays 1 (opaque)
            float mask = std::clamp(green_diff * 10.0f, 0.0f, 1.0f);
            alpha_hint(y, x, 0) = 1.0f - mask;
        }
    });
}

ImageBuffer ColorUtils::resize(const Image image, int new_width, int new_height) {
    ImageBuffer result(new_width, new_height, image.channels);
    Image res_view = result.view();

    float scale_x = static_cast<float>(image.width) / new_width;
    float scale_y = static_cast<float>(image.height) / new_height;

    for (int y = 0; y < new_height; ++y) {
        float src_y = (y + 0.5f) * scale_y - 0.5f;
        int y0 = std::max(0, static_cast<int>(std::floor(src_y)));
        int y1 = std::min(y0 + 1, image.height - 1);
        float dy = src_y - y0;

        for (int x = 0; x < new_width; ++x) {
            float src_x = (x + 0.5f) * scale_x - 0.5f;
            int x0 = std::max(0, static_cast<int>(std::floor(src_x)));
            int x1 = std::min(x0 + 1, image.width - 1);
            float dx = src_x - x0;

            for (int c = 0; c < image.channels; ++c) {
                float v00 = image(y0, x0, c);
                float v10 = image(y0, x1, c);
                float v01 = image(y1, x0, c);
                float v11 = image(y1, x1, c);

                float v0 = v00 * (1.0f - dx) + v10 * dx;
                float v1 = v01 * (1.0f - dx) + v11 * dx;
                res_view(y, x, c) = v0 * (1.0f - dy) + v1 * dy;
            }
        }
    }

    return result;
}

} // namespace corridorkey
