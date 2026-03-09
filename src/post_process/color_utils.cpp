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
    if (rgb.empty() || alpha.empty()) return;
    
    std::vector<int> rows(rgb.height);
    std::iota(rows.begin(), rows.end(), 0);

    // Modern 2D iteration with 0 manual index math
    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < rgb.width; ++x) {
            float a = alpha(y, x);
            for (int c = 0; c < 3; ++c) {
                rgb(y, x, c) *= a;
            }
        }
    });
}

void ColorUtils::unpremultiply(Image& rgb, const Image& alpha) {
    if (rgb.empty() || alpha.empty()) return;
    
    std::vector<int> rows(rgb.height);
    std::iota(rows.begin(), rows.end(), 0);

    // High-performance branchless unpremultiply
    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < rgb.width; ++x) {
            float a = alpha(y, x);
            // Branchless protection against division by zero
            // std::max ensures we never divide by less than 1e-6
            float inv_a = 1.0f / std::max(1e-6f, a);
            
            for (int c = 0; c < 3; ++c) {
                rgb(y, x, c) *= inv_a;
            }
        }
    });
}

void ColorUtils::to_planar(const Image& src, float* dst) {
    int w = src.width;
    int h = src.height;
    int c = src.channels;

    std::vector<int> channels(c);
    std::iota(channels.begin(), channels.end(), 0);

    std::for_each(EXEC_POLICY channels.begin(), channels.end(), [&](int ch) {
        float* plane = dst + (static_cast<size_t>(ch) * w * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                plane[static_cast<size_t>(y) * w + x] = src(y, x, ch);
            }
        }
    });
}

void ColorUtils::from_planar(const float* src, Image& dst) {
    int w = dst.width;
    int h = dst.height;
    int c = dst.channels;

    std::vector<int> channels(c);
    std::iota(channels.begin(), channels.end(), 0);

    std::for_each(EXEC_POLICY channels.begin(), channels.end(), [&](int ch) {
        const float* plane = src + (static_cast<size_t>(ch) * w * h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                dst(y, x, ch) = plane[static_cast<size_t>(y) * w + x];
            }
        }
    });
}

void ColorUtils::despill(Image& rgb, const Image& alpha, float strength) {
    if (rgb.empty() || alpha.empty()) return;

    std::vector<int> rows(rgb.height);
    std::iota(rows.begin(), rows.end(), 0);

    // High-performance branchless despill (2026 elite standard)
    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < rgb.width; ++x) {
            float r = rgb(y, x, 0);
            float g = rgb(y, x, 1);
            float b = rgb(y, x, 2);
            float a = alpha(y, x);

            // Target green is the average of R and B
            float target_g = (r + b) * 0.5f;
            
            // Branchless: compute the reduction amount
            // If g > target_g, diff is positive, otherwise 0
            float diff = std::max(0.0f, g - target_g);
            
            // Apply reduction scaled by alpha and strength
            // This math naturally results in 0 change if a == 0 or g <= target_g
            rgb(y, x, 1) = g - (diff * strength * std::min(1.0f, a));
        }
    });
}

void ColorUtils::despeckle(Image& alpha, int size_threshold) {
    if (alpha.empty() || size_threshold <= 0) return;

    ImageBuffer temp_buffer(alpha.width, alpha.height, 1);
    Image temp = temp_buffer.view();

    std::vector<int> rows(alpha.height);
    std::iota(rows.begin(), rows.end(), 0);

    // Erosion
    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < alpha.width; ++x) {
            float min_v = 1.0f;
            for (int dy : {-1, 0, 1}) {
                for (int dx : {-1, 0, 1}) {
                    min_v = std::min(min_v, alpha(std::clamp(y + dy, 0, alpha.height - 1), 
                                                  std::clamp(x + dx, 0, alpha.width - 1)));
                }
            }
            temp(y, x) = min_v;
        }
    });

    // Dilation
    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < alpha.width; ++x) {
            float max_v = 0.0f;
            for (int dy : {-1, 0, 1}) {
                for (int dx : {-1, 0, 1}) {
                    max_v = std::max(max_v, temp(std::clamp(y + dy, 0, alpha.height - 1), 
                                                 std::clamp(x + dx, 0, alpha.width - 1)));
                }
            }
            alpha(y, x) = max_v;
        }
    });
}

void ColorUtils::composite_over_checker(Image& rgba) {
    if (rgba.empty() || rgba.channels < 4) return;

    std::vector<int> rows(rgba.height);
    std::iota(rows.begin(), rows.end(), 0);

    // High-performance branchless composite (2026 elite standard)
    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
        for (int x = 0; x < rgba.width; ++x) {
            float alpha = std::clamp(rgba(y, x, 3), 0.0f, 1.0f);
            
            // Branchless checkerboard: calculate pattern based on parity of coordinates
            // (x >> 4) is equivalent to (x / 16) for positive integers
            bool is_white = ((x >> 4) + (y >> 4)) % 2 == 0;
            float bg = 0.2f + (0.2f * static_cast<float>(is_white));

            // Pure arithmetic blend
            float inv_alpha = 1.0f - alpha;
            rgba(y, x, 0) = rgba(y, x, 0) * alpha + bg * inv_alpha;
            rgba(y, x, 1) = rgba(y, x, 1) * alpha + bg * inv_alpha;
            rgba(y, x, 2) = rgba(y, x, 2) * alpha + bg * inv_alpha;
            rgba(y, x, 3) = 1.0f; 
        }
    });
}

ImageBuffer ColorUtils::resize(const Image& image, int new_width, int new_height) {
    ImageBuffer result(new_width, new_height, image.channels);
    Image res_view = result.view();

    float scale_x = static_cast<float>(image.width) / new_width;
    float scale_y = static_cast<float>(image.height) / new_height;
    
    std::vector<int> rows(new_height);
    std::iota(rows.begin(), rows.end(), 0);

    std::for_each(EXEC_POLICY rows.begin(), rows.end(), [&](int y) {
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
    });

    return result;
}

} // namespace corridorkey
