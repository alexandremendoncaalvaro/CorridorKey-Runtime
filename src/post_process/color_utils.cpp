#include <src/post_process/color_utils.hpp>
#include <cmath>
#include <algorithm>

namespace corridorkey {

void ColorUtils::srgb_to_linear(Image& image) {
    for (float& p : image.data) {
        if (p <= 0.04045f) {
            p = p / 12.92f;
        } else {
            p = std::pow((p + 0.055f) / 1.055f, 2.4f);
        }
    }
}

void ColorUtils::linear_to_srgb(Image& image) {
    for (float& p : image.data) {
        if (p <= 0.0031308f) {
            p = p * 12.92f;
        } else {
            p = 1.055f * std::pow(p, 1.0f / 2.4f) - 0.055f;
        }
    }
}

void ColorUtils::premultiply(Image& rgb, const Image& alpha) {
    if (rgb.width != alpha.width || rgb.height != alpha.height) return;
    
    for (int i = 0; i < rgb.width * rgb.height; ++i) {
        float a = alpha.data[i];
        rgb.data[i * 3 + 0] *= a;
        rgb.data[i * 3 + 1] *= a;
        rgb.data[i * 3 + 2] *= a;
    }
}

void ColorUtils::unpremultiply(Image& rgb, const Image& alpha) {
    if (rgb.width != alpha.width || rgb.height != alpha.height) return;
    
    for (int i = 0; i < rgb.width * rgb.height; ++i) {
        float a = alpha.data[i];
        if (a > 0.0001f) {
            rgb.data[i * 3 + 0] /= a;
            rgb.data[i * 3 + 1] /= a;
            rgb.data[i * 3 + 2] /= a;
        }
    }
}

void ColorUtils::despill(Image& rgb, const Image& alpha, float strength) {
    (void)rgb;
    (void)alpha;
    (void)strength;
    // TODO: Port despill logic from CorridorKey (luminance preserving)
}

void ColorUtils::despeckle(Image& alpha, int size_threshold) {
    (void)alpha;
    (void)size_threshold;
    // TODO: Port despeckle logic (morphological cleanup)
}

void ColorUtils::composite_over_checker(Image& rgba) {
    (void)rgba;
    // TODO: Implement checkerboard compositing for preview
}

} // namespace corridorkey
