#include "post_process/color_utils.hpp"
#include "core/perf_utils.hpp"
#include <cmath>
#include <algorithm>
#include <numeric>

// Execution policy wrapper for portability
#if __has_include(<execution>) && (defined(__cpp_lib_execution) || !defined(__clang__))
#include <execution>
#define EXEC_POLICY std::execution::par_unseq ,
#else
#define EXEC_POLICY
#endif

namespace corridorkey {

void ColorUtils::srgb_to_linear(Image& image) {
    const auto& lut = SrgbLut::instance();
    std::for_each(EXEC_POLICY image.data.begin(), image.data.end(), [&](float& p) {
        p = lut.to_linear(p);
    });
}

void ColorUtils::linear_to_srgb(Image& image) {
    const auto& lut = SrgbLut::instance();
    std::for_each(EXEC_POLICY image.data.begin(), image.data.end(), [&](float& p) {
        p = lut.to_srgb(p);
    });
}

void ColorUtils::premultiply(Image& rgb, const Image& alpha) {
    if (rgb.empty() || alpha.empty()) return;
    
    size_t pixels = static_cast<size_t>(rgb.width) * rgb.height;
    int channels = rgb.channels;

    // Linear loop for maximum SIMD throughput
    for (size_t i = 0; i < pixels; ++i) {
        float a = alpha.data[i];
        float* p = &rgb.data[i * channels];
        p[0] *= a;
        p[1] *= a;
        p[2] *= a;
    }
}

void ColorUtils::unpremultiply(Image& rgb, const Image& alpha) {
    if (rgb.empty() || alpha.empty()) return;
    
    size_t pixels = static_cast<size_t>(rgb.width) * rgb.height;
    int channels = rgb.channels;

    for (size_t i = 0; i < pixels; ++i) {
        float inv_a = 1.0f / std::max(1e-6f, alpha.data[i]);
        float* p = &rgb.data[i * channels];
        p[0] *= inv_a;
        p[1] *= inv_a;
        p[2] *= inv_a;
    }
}

void ColorUtils::to_planar(const Image src, float* dst) {
    int w = src.width;
    int h = src.height;
    int c = src.channels;
    size_t plane_size = static_cast<size_t>(w) * h;

    for (int ch = 0; ch < c; ++ch) {
        float* plane = dst + (ch * plane_size);
        for (size_t i = 0; i < plane_size; ++i) {
            plane[i] = src.data[i * c + ch];
        }
    }
}

void ColorUtils::from_planar(const float* src, Image dst) {
    int w = dst.width;
    int h = dst.height;
    int c = dst.channels;
    size_t plane_size = static_cast<size_t>(w) * h;

    for (int ch = 0; ch < c; ++ch) {
        const float* plane = src + (ch * plane_size);
        for (size_t i = 0; i < plane_size; ++i) {
            dst.data[i * c + ch] = plane[i];
        }
    }
}

void ColorUtils::despill(Image& rgb, const Image& alpha, float strength) {
    if (rgb.empty() || alpha.empty()) return;

    size_t pixels = static_cast<size_t>(rgb.width) * rgb.height;
    int channels = rgb.channels;

    for (size_t i = 0; i < pixels; ++i) {
        float* p = &rgb.data[i * channels];
        float target_g = (p[0] + p[2]) * 0.5f;
        float diff = std::max(0.0f, p[1] - target_g);
        p[1] -= diff * strength * std::min(1.0f, alpha.data[i]);
    }
}

void ColorUtils::composite_over_checker(Image& rgba) {
    if (rgba.empty() || rgba.channels < 4) return;

    int w = rgba.width;
    // Spatial awareness is needed for checkerboard pattern, but we flatten the X loop
    for (int y = 0; y < rgba.height; ++y) {
        int y_check = (y >> 4) & 1;
        float* row = &rgba.data[static_cast<size_t>(y) * w * 4];
        for (int x = 0; x < w; ++x) {
            float* p = row + (x * 4);
            float a = std::clamp(p[3], 0.0f, 1.0f);
            float bg = (((x >> 4) & 1) == y_check) ? 0.4f : 0.2f;
            
            p[0] = p[0] * a + bg * (1.0f - a);
            p[1] = p[1] * a + bg * (1.0f - a);
            p[2] = p[2] * a + bg * (1.0f - a);
            p[3] = 1.0f;
        }
    }
}

void ColorUtils::despeckle(Image& alpha, int size_threshold) {
    if (alpha.empty() || size_threshold <= 0) return;

    ImageBuffer temp_buffer(alpha.width, alpha.height, 1);
    Image temp = temp_buffer.view();
    int w = alpha.width;
    int h = alpha.height;

    // Erosion
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float min_v = 1.0f;
            for (int dy : {-1, 0, 1}) {
                int ny = std::clamp(y + dy, 0, h - 1);
                for (int dx : {-1, 0, 1}) {
                    min_v = std::min(min_v, alpha(ny, std::clamp(x + dx, 0, w - 1)));
                }
            }
            temp(y, x) = min_v;
        }
    }

    // Dilation
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float max_v = 0.0f;
            for (int dy : {-1, 0, 1}) {
                int ny = std::clamp(y + dy, 0, h - 1);
                for (int dx : {-1, 0, 1}) {
                    max_v = std::max(max_v, temp(ny, std::clamp(x + dx, 0, w - 1)));
                }
            }
            alpha(y, x) = max_v;
        }
    }
}

ImageBuffer ColorUtils::resize(const Image& image, int new_width, int new_height) {
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
