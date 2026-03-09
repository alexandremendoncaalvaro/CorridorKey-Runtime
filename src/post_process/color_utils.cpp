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
    (void)rgb; (void)alpha; (void)strength;
}

void ColorUtils::despeckle(Image& alpha, int size_threshold) {
    (void)alpha; (void)size_threshold;
}

void ColorUtils::composite_over_checker(Image& rgba) {
    (void)rgba;
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
