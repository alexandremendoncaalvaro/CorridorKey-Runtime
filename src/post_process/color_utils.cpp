#include "color_utils.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "common/srgb_lut.hpp"

namespace corridorkey {

void ColorUtils::premultiply(Image rgb, Image alpha) {
    for (int y_pos = 0; y_pos < rgb.height; ++y_pos) {
        for (int x_pos = 0; x_pos < rgb.width; ++x_pos) {
            const float alpha_val = alpha(y_pos, x_pos, 0);
            rgb(y_pos, x_pos, 0) *= alpha_val;
            rgb(y_pos, x_pos, 1) *= alpha_val;
            rgb(y_pos, x_pos, 2) *= alpha_val;
        }
    }
}

void ColorUtils::unpremultiply(Image rgb, Image alpha) {
    for (int y_pos = 0; y_pos < rgb.height; ++y_pos) {
        for (int x_pos = 0; x_pos < rgb.width; ++x_pos) {
            const float alpha_val = alpha(y_pos, x_pos, 0);
            if (alpha_val > 0.0F) {
                rgb(y_pos, x_pos, 0) /= alpha_val;
                rgb(y_pos, x_pos, 1) /= alpha_val;
                rgb(y_pos, x_pos, 2) /= alpha_val;
            }
        }
    }
}

void ColorUtils::composite_over_checker(Image rgba) {
    const SrgbLut& lut = SrgbLut::instance();
    const float bg_dark = lut.to_linear(0.15F);
    const float bg_light = lut.to_linear(0.55F);

    const int height = rgba.height;
    const int width = rgba.width;

    for (int y_pos = 0; y_pos < height; ++y_pos) {
        for (int x_pos = 0; x_pos < width; ++x_pos) {
            const float alpha = rgba(y_pos, x_pos, 3);
            const bool is_dark = ((y_pos / 16) + (x_pos / 16)) % 2 == 0;
            const float bg_val = is_dark ? bg_dark : bg_light;

            rgba(y_pos, x_pos, 0) = rgba(y_pos, x_pos, 0) * alpha + bg_val * (1.0F - alpha);
            rgba(y_pos, x_pos, 1) = rgba(y_pos, x_pos, 1) * alpha + bg_val * (1.0F - alpha);
            rgba(y_pos, x_pos, 2) = rgba(y_pos, x_pos, 2) * alpha + bg_val * (1.0F - alpha);
            rgba(y_pos, x_pos, 3) = 1.0F;
        }
    }
}

void ColorUtils::generate_rough_matte(Image rgb, Image alpha_hint) {
    const int height = rgb.height;
    const int width = rgb.width;

    for (int y_pos = 0; y_pos < height; ++y_pos) {
        for (int x_pos = 0; x_pos < width; ++x_pos) {
            const float red = rgb(y_pos, x_pos, 0);
            const float green = rgb(y_pos, x_pos, 1);
            const float blue = rgb(y_pos, x_pos, 2);

            // Simple green-detecting heuristic
            const float green_bias = green - std::max(red, blue);
            float matte = 1.0F - std::clamp(green_bias * 2.0F, 0.0F, 1.0F);

            alpha_hint(y_pos, x_pos, 0) = matte;
        }
    }
}

ImageBuffer ColorUtils::resize(Image image, int new_width, int new_height) {
    ImageBuffer result(new_width, new_height, image.channels);
    Image res_view = result.view();

    const float scale_x = static_cast<float>(image.width) / static_cast<float>(new_width);
    const float scale_y = static_cast<float>(image.height) / static_cast<float>(new_height);

    for (int y_pos = 0; y_pos < new_height; ++y_pos) {
        const float src_y = (static_cast<float>(y_pos) + 0.5F) * scale_y - 0.5F;
        const int y0 = std::max(0, static_cast<int>(std::floor(src_y)));
        const int y1 = std::min(y0 + 1, image.height - 1);
        const float d_y = src_y - static_cast<float>(y0);

        for (int x_pos = 0; x_pos < new_width; ++x_pos) {
            const float src_x = (static_cast<float>(x_pos) + 0.5F) * scale_x - 0.5F;
            const int x0 = std::max(0, static_cast<int>(std::floor(src_x)));
            const int x1 = std::min(x0 + 1, image.width - 1);
            const float d_x = src_x - static_cast<float>(x0);

            for (int channel = 0; channel < image.channels; ++channel) {
                const float v00 = image(y0, x0, channel);
                const float v10 = image(y0, x1, channel);
                const float v01 = image(y1, x0, channel);
                const float v11 = image(y1, x1, channel);

                const float val_y0 = v00 * (1.0F - d_x) + v10 * d_x;
                const float val_y1 = v01 * (1.0F - d_x) + v11 * d_x;
                res_view(y_pos, x_pos, channel) = val_y0 * (1.0F - d_y) + val_y1 * d_y;
            }
        }
    }

    return result;
}

void ColorUtils::gaussian_blur(Image image, float sigma) {
    if (image.empty() || sigma <= 0.0F) return;

    int w = image.width;
    int h = image.height;
    int channels = image.channels;
    int kernel = static_cast<int>(std::ceil(sigma * 3.0F));
    if (kernel < 1) kernel = 1;

    std::vector<float> weights(static_cast<size_t>(kernel) + 1);
    float sum = 0.0F;
    for (int i = 0; i <= kernel; ++i) {
        float fi = static_cast<float>(i);
        weights[i] = std::exp(-(fi * fi) / (2.0F * sigma * sigma));
        sum += (i == 0) ? weights[i] : 2.0F * weights[i];
    }
    for (int i = 0; i <= kernel; ++i) {
        weights[i] /= sum;
    }

    size_t buf_size = static_cast<size_t>(w) * h * channels;
    std::vector<float> temp(buf_size);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < channels; ++c) {
                float acc = image(y, x, c) * weights[0];
                for (int dx = 1; dx <= kernel; ++dx) {
                    int xl = std::max(x - dx, 0);
                    int xr = std::min(x + dx, w - 1);
                    acc += (image(y, xl, c) + image(y, xr, c)) * weights[dx];
                }
                temp[(static_cast<size_t>(y) * w + x) * channels + c] = acc;
            }
        }
    }

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < channels; ++c) {
                size_t idx = (static_cast<size_t>(y) * w + x) * channels + c;
                float acc = temp[idx] * weights[0];
                for (int dy = 1; dy <= kernel; ++dy) {
                    int yt = std::max(y - dy, 0);
                    int yb = std::min(y + dy, h - 1);
                    acc += (temp[(static_cast<size_t>(yt) * w + x) * channels + c] +
                            temp[(static_cast<size_t>(yb) * w + x) * channels + c]) *
                           weights[dy];
                }
                image(y, x, c) = acc;
            }
        }
    }
}

ImageBuffer ColorUtils::resize_area(Image image, int new_width, int new_height) {
    float scale_x = static_cast<float>(image.width) / static_cast<float>(new_width);
    float scale_y = static_cast<float>(image.height) / static_cast<float>(new_height);
    float max_scale = std::max(scale_x, scale_y);

    if (max_scale <= 1.5F) {
        return resize(image, new_width, new_height);
    }

    float sigma = (max_scale - 1.0F) * 0.5F;

    ImageBuffer blurred(image.width, image.height, image.channels);
    Image blurred_view = blurred.view();
    std::copy(image.data.begin(), image.data.end(), blurred_view.data.begin());
    gaussian_blur(blurred_view, sigma);

    return resize(blurred_view, new_width, new_height);
}

namespace {

constexpr int kLanczosA = 4;
constexpr float kPi = 3.14159265358979323846F;

float lanczos_kernel(float x) {
    if (x == 0.0F) return 1.0F;
    if (x >= kLanczosA || x <= -kLanczosA) return 0.0F;
    const float pi_x = kPi * x;
    const float pi_x_a = pi_x / kLanczosA;
    return (std::sin(pi_x) / pi_x) * (std::sin(pi_x_a) / pi_x_a);
}

// BORDER_REFLECT_101: gfedcb|abcdefgh|gfedcba (matches OpenCV default)
inline int reflect_101(int idx, int len) {
    if (idx < 0) idx = -idx;
    if (idx >= len) idx = 2 * (len - 1) - idx;
    return std::clamp(idx, 0, len - 1);
}

}  // namespace

ImageBuffer ColorUtils::resize_lanczos(Image image, int new_width, int new_height) {
    const float scale_x = static_cast<float>(image.width) / static_cast<float>(new_width);
    const float scale_y = static_cast<float>(image.height) / static_cast<float>(new_height);

    // Horizontal pass: image (H x W) -> intermediate (H x new_width)
    ImageBuffer h_buf(new_width, image.height, image.channels);
    Image h_view = h_buf.view();

    for (int y_pos = 0; y_pos < image.height; ++y_pos) {
        for (int x_pos = 0; x_pos < new_width; ++x_pos) {
            const float center = (static_cast<float>(x_pos) + 0.5F) * scale_x - 0.5F;
            const int i_start = static_cast<int>(std::floor(center - kLanczosA + 1));
            const int i_end = static_cast<int>(std::floor(center + kLanczosA));

            float weight_sum = 0.0F;
            for (int c = 0; c < image.channels; ++c) {
                h_view(y_pos, x_pos, c) = 0.0F;
            }

            for (int i = i_start; i <= i_end; ++i) {
                const int src_i = reflect_101(i, image.width);
                const float w = lanczos_kernel(static_cast<float>(i) - center);
                weight_sum += w;
                for (int c = 0; c < image.channels; ++c) {
                    h_view(y_pos, x_pos, c) += image(y_pos, src_i, c) * w;
                }
            }

            if (weight_sum > 0.0F) {
                const float inv_w = 1.0F / weight_sum;
                for (int c = 0; c < image.channels; ++c) {
                    h_view(y_pos, x_pos, c) *= inv_w;
                }
            }
        }
    }

    // Vertical pass: intermediate (H x new_width) -> result (new_height x new_width)
    ImageBuffer result(new_width, new_height, image.channels);
    Image res_view = result.view();

    for (int y_pos = 0; y_pos < new_height; ++y_pos) {
        const float center = (static_cast<float>(y_pos) + 0.5F) * scale_y - 0.5F;
        const int j_start = static_cast<int>(std::floor(center - kLanczosA + 1));
        const int j_end = static_cast<int>(std::floor(center + kLanczosA));

        for (int x_pos = 0; x_pos < new_width; ++x_pos) {
            float weight_sum = 0.0F;
            for (int c = 0; c < image.channels; ++c) {
                res_view(y_pos, x_pos, c) = 0.0F;
            }

            for (int j = j_start; j <= j_end; ++j) {
                const int src_j = reflect_101(j, h_view.height);
                const float w = lanczos_kernel(static_cast<float>(j) - center);
                weight_sum += w;
                for (int c = 0; c < image.channels; ++c) {
                    res_view(y_pos, x_pos, c) += h_view(src_j, x_pos, c) * w;
                }
            }

            if (weight_sum > 0.0F) {
                const float inv_w = 1.0F / weight_sum;
                for (int c = 0; c < image.channels; ++c) {
                    res_view(y_pos, x_pos, c) *= inv_w;
                }
            }
        }
    }

    return result;
}

void ColorUtils::clamp_image(Image image, float min_val, float max_val) {
    for (auto& v : image.data) {
        v = std::clamp(v, min_val, max_val);
    }
}

std::pair<ImageBuffer, Rect> ColorUtils::fit_pad(Image image, int target_width, int target_height) {
    const float scale =
        std::min(static_cast<float>(target_width) / static_cast<float>(image.width),
                 static_cast<float>(target_height) / static_cast<float>(image.height));
    const int new_w = static_cast<int>(static_cast<float>(image.width) * scale);
    const int new_h = static_cast<int>(static_cast<float>(image.height) * scale);

    ImageBuffer resized = resize(image, new_w, new_h);

    ImageBuffer result(target_width, target_height, image.channels);
    std::fill(result.view().data.begin(), result.view().data.end(), 0.0F);

    const int pad_x = (target_width - new_w) / 2;
    const int pad_y = (target_height - new_h) / 2;

    Image dst = result.view();
    Image src = resized.view();

    for (int y_pos = 0; y_pos < new_h; ++y_pos) {
        for (int x_pos = 0; x_pos < new_w; ++x_pos) {
            for (int channel = 0; channel < image.channels; ++channel) {
                dst(y_pos + pad_y, x_pos + pad_x, channel) = src(y_pos, x_pos, channel);
            }
        }
    }

    return std::make_pair(std::move(result), Rect{pad_x, pad_y, new_w, new_h});
}

ImageBuffer ColorUtils::crop(Image image, int x_start, int y_start, int width, int height) {
    ImageBuffer result(width, height, image.channels);
    Image dst = result.view();

    for (int y_pos = 0; y_pos < height; ++y_pos) {
        for (int x_pos = 0; x_pos < width; ++x_pos) {
            for (int channel = 0; channel < image.channels; ++channel) {
                dst(y_pos, x_pos, channel) = image(y_pos + y_start, x_pos + x_start, channel);
            }
        }
    }

    return result;
}

void ColorUtils::to_planar(Image src, float* dst) {
    const int height = src.height;
    const int width = src.width;
    const int channels = src.channels;
    const size_t channel_stride = static_cast<size_t>(height) * width;

    for (int channel = 0; channel < channels; ++channel) {
        for (int y_pos = 0; y_pos < height; ++y_pos) {
            for (int x_pos = 0; x_pos < width; ++x_pos) {
                dst[static_cast<size_t>(channel) * channel_stride +
                    (static_cast<size_t>(y_pos) * width + x_pos)] = src(y_pos, x_pos, channel);
            }
        }
    }
}

void ColorUtils::from_planar(const float* src, Image dst) {
    const int height = dst.height;
    const int width = dst.width;
    const int channels = dst.channels;
    const size_t channel_stride = static_cast<size_t>(height) * width;

    for (int channel = 0; channel < channels; ++channel) {
        for (int y_pos = 0; y_pos < height; ++y_pos) {
            for (int x_pos = 0; x_pos < width; ++x_pos) {
                dst(y_pos, x_pos, channel) = src[static_cast<size_t>(channel) * channel_stride +
                                                 (static_cast<size_t>(y_pos) * width + x_pos)];
            }
        }
    }
}

void ColorUtils::srgb_to_linear(Image image) {
    const SrgbLut& lut = SrgbLut::instance();
    for (float& val : image.data) {
        val = lut.to_linear(val);
    }
}

void ColorUtils::linear_to_srgb(Image image) {
    const SrgbLut& lut = SrgbLut::instance();
    for (float& val : image.data) {
        val = lut.to_srgb(val);
    }
}

}  // namespace corridorkey
