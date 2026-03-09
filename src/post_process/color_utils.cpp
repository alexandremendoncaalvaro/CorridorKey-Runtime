#include <src/post_process/color_utils.hpp>
#include <cmath>
#include <algorithm>
#include <array>
#include <numeric>

// Portability check for execution policy support
#if __has_include(<execution>) && (defined(__cpp_lib_execution) || !defined(__clang__))
#include <execution>
#define EXEC_POLICY std::execution::par_unseq ,
#else
#define EXEC_POLICY
#endif

namespace corridorkey {

// --- High Performance Lookup Tables (LUT) ---

/**
 * Pre-calculating sRGB to Linear for 12-bit precision (4096 entries).
 * This fits perfectly in L1/L2 cache and is orders of magnitude faster than std::pow.
 * Inspired by llamafile's approach to table-based quantization and conversion.
 */
class SrgbLut {
public:
    static const SrgbLut& instance() {
        static SrgbLut lut;
        return lut;
    }

    inline float to_linear(float srgb) const {
        if (srgb <= 0.0f) return 0.0f;
        if (srgb >= 1.0f) return 1.0f;
        int idx = static_cast<int>(srgb * 4095.0f);
        return m_to_linear[idx];
    }

private:
    SrgbLut() {
        for (int i = 0; i <= 4095; ++i) {
            float p = i / 4095.0f;
            m_to_linear[i] = (p <= 0.04045f) ? (p / 12.92f) : std::pow((p + 0.055f) / 1.055f, 2.4f);
        }
    }
    std::array<float, 4096> m_to_linear;
};

void ColorUtils::srgb_to_linear(Image& image) {
    const auto& lut = SrgbLut::instance();
    std::for_each(EXEC_POLICY image.data.begin(), image.data.end(), [&](float& p) {
        p = lut.to_linear(p);
    });
}

void ColorUtils::linear_to_srgb(Image& image) {
    std::for_each(EXEC_POLICY image.data.begin(), image.data.end(), [](float& p) {
        if (p <= 0.0031308f) {
            p = p * 12.92f;
        } else {
            p = 1.055f * std::pow(p, 1.0f / 2.4f) - 0.055f;
        }
    });
}

void ColorUtils::premultiply(Image& rgb, const Image& alpha) {
    if (rgb.width != alpha.width || rgb.height != alpha.height) return;
    
    int total_pixels = rgb.width * rgb.height;
    int channels = rgb.channels;
    std::vector<int> indices(total_pixels);
    std::iota(indices.begin(), indices.end(), 0);

    // Flattened loop with parallel vector execution
    std::for_each(EXEC_POLICY indices.begin(), indices.end(), [&](int i) {
        float a = alpha.data[i];
        float* p = &rgb.data[i * channels];
        p[0] *= a; p[1] *= a; p[2] *= a;
    });
}

void ColorUtils::unpremultiply(Image& rgb, const Image& alpha) {
    if (rgb.width != alpha.width || rgb.height != alpha.height) return;
    
    int total_pixels = rgb.width * rgb.height;
    int channels = rgb.channels;
    std::vector<int> indices(total_pixels);
    std::iota(indices.begin(), indices.end(), 0);

    std::for_each(EXEC_POLICY indices.begin(), indices.end(), [&](int i) {
        float a = alpha.data[i];
        if (a > 0.0001f) {
            float* p = &rgb.data[i * channels];
            float inv_a = 1.0f / a;
            p[0] *= inv_a; p[1] *= inv_a; p[2] *= inv_a;
        }
    });
}

void ColorUtils::to_planar(const Image& src, float* dst) {
    int w = src.width;
    int h = src.height;
    int c = src.channels;

    for (int ch = 0; ch < c; ++ch) {
        float* plane = dst + ch * w * h;
        for (int i = 0; i < w * h; ++i) {
            plane[i] = src.data[i * c + ch];
        }
    }
}

void ColorUtils::from_planar(const float* src, Image& dst) {
    int w = dst.width;
    int h = dst.height;
    int c = dst.channels;

    for (int ch = 0; ch < c; ++ch) {
        const float* plane = src + ch * w * h;
        for (int i = 0; i < w * h; ++i) {
            dst.data[i * c + ch] = plane[i];
        }
    }
}

void ColorUtils::despill(Image& rgb, const Image& alpha, float strength) {
    if (rgb.empty() || alpha.empty() || rgb.width != alpha.width || rgb.height != alpha.height) return;

    int total_pixels = rgb.width * rgb.height;
    int channels = rgb.channels;
    std::vector<int> indices(total_pixels);
    std::iota(indices.begin(), indices.end(), 0);

    // Simple but effective luminance-preserving green despill
    std::for_each(EXEC_POLICY indices.begin(), indices.end(), [&](int i) {
        float* p = &rgb.data[i * channels];
        float a = alpha.data[i];

        // Only despill where there is some foreground
        if (a > 0.0f) {
            float r = p[0];
            float g = p[1];
            float b = p[2];

            // Average of red and blue as target for green
            float target_g = (r + b) * 0.5f;
            
            if (g > target_g) {
                // Blend based on strength
                float despilled_g = target_g * strength + g * (1.0f - strength);
                p[1] = std::min(g, despilled_g);
            }
        }
    });
}

void ColorUtils::despeckle(Image& alpha, int size_threshold) {
    if (alpha.empty() || size_threshold <= 0) return;

    int w = alpha.width;
    int h = alpha.height;
    
    ImageBuffer temp_buffer(w, h, 1);
    Image temp = temp_buffer.view();

    std::vector<int> rows(h);
    std::iota(rows.begin(), rows.end(), 0);

    // 1. Erosion (3x3 min filter)
    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < w; ++x) {
            float min_val = 1.0f;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = std::clamp(x + dx, 0, w - 1);
                    int ny = std::clamp(y + dy, 0, h - 1);
                    min_val = std::min(min_val, alpha.data[ny * w + nx]);
                }
            }
            temp.data[y * w + x] = min_val;
        }
    });

    // 2. Dilation (3x3 max filter)
    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < w; ++x) {
            float max_val = 0.0f;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = std::clamp(x + dx, 0, w - 1);
                    int ny = std::clamp(y + dy, 0, h - 1);
                    max_val = std::max(max_val, temp.data[ny * w + nx]);
                }
            }
            alpha.data[y * w + x] = max_val;
        }
    });
}

void ColorUtils::composite_over_checker(Image& rgba) {
    if (rgba.empty() || rgba.channels < 4) return;

    int w = rgba.width;
    int h = rgba.height;
    std::vector<int> rows(h);
    std::iota(rows.begin(), rows.end(), 0);

    // Checkerboard pattern (16px squares)
    const float gray1 = 0.2f;
    const float gray2 = 0.4f;

    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < w; ++x) {
            float* p = &rgba.data[(y * w + x) * 4];
            float alpha = p[3];

            // Determine checker color
            bool is_white = ((x / 16) + (y / 16)) % 2 == 0;
            float bg = is_white ? gray2 : gray1;

            // Simple over composite: result = fg * alpha + bg * (1 - alpha)
            p[0] = p[0] * alpha + bg * (1.0f - alpha);
            p[1] = p[1] * alpha + bg * (1.0f - alpha);
            p[2] = p[2] * alpha + bg * (1.0f - alpha);
            p[3] = 1.0f; // Preview is opaque
        }
    });
}

ImageBuffer ColorUtils::resize(const Image& image, int new_width, int new_height) {
    ImageBuffer result(new_width, new_height, image.channels);
    Image result_view = result.view();

    float scale_x = (float)image.width / new_width;
    float scale_y = (float)image.height / new_height;

    for (int y = 0; y < new_height; ++y) {
        float src_y = (y + 0.5f) * scale_y - 0.5f;
        int y0 = std::max(0, (int)std::floor(src_y));
        int y1 = std::min(y0 + 1, image.height - 1);
        float dy = src_y - y0;

        for (int x = 0; x < new_width; ++x) {
            float src_x = (x + 0.5f) * scale_x - 0.5f;
            int x0 = std::max(0, (int)std::floor(src_x));
            int x1 = std::min(x0 + 1, image.width - 1);
            float dx = src_x - x0;

            for (int c = 0; c < image.channels; ++c) {
                float v00 = image.data[(y0 * image.width + x0) * image.channels + c];
                float v10 = image.data[(y0 * image.width + x1) * image.channels + c];
                float v01 = image.data[(y1 * image.width + x0) * image.channels + c];
                float v11 = image.data[(y1 * image.width + x1) * image.channels + c];

                float v0 = v00 * (1.0f - dx) + v10 * dx;
                float v1 = v01 * (1.0f - dx) + v11 * dx;
                result_view.data[(y * new_width + x) * result_view.channels + c] = v0 * (1.0f - dy) + v1 * dy;
            }
        }
    }

    return result;
}

} // namespace corridorkey
