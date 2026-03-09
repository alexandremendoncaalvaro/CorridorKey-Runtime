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

Image ColorUtils::resize(const Image& image, int new_width, int new_height) {
    Image result;
    result.width = new_width;
    result.height = new_height;
    result.channels = image.channels;
    result.data.resize(new_width * new_height * image.channels);

    float scale_x = (float)image.width / new_width;
    float scale_y = (float)image.height / new_height;

    for (int y = 0; y < new_height; ++y) {
        for (int x = 0; x < new_width; ++x) {
            float src_x = (x + 0.5f) * scale_x - 0.5f;
            float src_y = (y + 0.5f) * scale_y - 0.5f;

            int x0 = (int)std::floor(src_x);
            int y0 = (int)std::floor(src_y);
            int x1 = std::min(x0 + 1, image.width - 1);
            int y1 = std::min(y0 + 1, image.height - 1);
            x0 = std::max(0, x0);
            y0 = std::max(0, y0);

            float dx = src_x - x0;
            float dy = src_y - y0;

            for (int c = 0; c < image.channels; ++c) {
                float v00 = image.data[(y0 * image.width + x0) * image.channels + c];
                float v10 = image.data[(y0 * image.width + x1) * image.channels + c];
                float v01 = image.data[(y1 * image.width + x0) * image.channels + c];
                float v11 = image.data[(y1 * image.width + x1) * image.channels + c];

                float v0 = v00 * (1.0f - dx) + v10 * dx;
                float v1 = v01 * (1.0f - dx) + v11 * dx;
                result.data[(y * new_width + x) * result.channels + c] = v0 * (1.0f - dy) + v1 * dy;
            }
        }
    }

    return result;
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

} // namespace corridorkey
