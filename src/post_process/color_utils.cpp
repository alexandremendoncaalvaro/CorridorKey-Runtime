#include "color_utils.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "common/parallel_for.hpp"
#include "common/srgb_lut.hpp"

namespace corridorkey {

namespace {

float default_resized_channel_value(int channel, int total_channels) {
    if (total_channels == 4 && channel == 3) {
        return 1.0F;
    }
    return 0.0F;
}

void fill_default_channels(Image dst, int y_begin, int y_end, int begin_channel) {
    if (begin_channel >= dst.channels) {
        return;
    }

    for (int y_pos = y_begin; y_pos < y_end; ++y_pos) {
        for (int x_pos = 0; x_pos < dst.width; ++x_pos) {
            for (int channel = begin_channel; channel < dst.channels; ++channel) {
                dst(y_pos, x_pos, channel) = default_resized_channel_value(channel, dst.channels);
            }
        }
    }
}

void prepare_bilinear_axis(int src_size, int dst_size, std::vector<int>& lower_indices,
                           std::vector<int>& upper_indices, std::vector<float>& weights) {
    lower_indices.resize(static_cast<size_t>(dst_size));
    upper_indices.resize(static_cast<size_t>(dst_size));
    weights.resize(static_cast<size_t>(dst_size));

    const float scale = static_cast<float>(src_size) / static_cast<float>(dst_size);
    for (int dst_index = 0; dst_index < dst_size; ++dst_index) {
        const float src_pos = (static_cast<float>(dst_index) + 0.5F) * scale - 0.5F;
        const int lower = std::max(0, static_cast<int>(std::floor(src_pos)));
        const int upper = std::min(lower + 1, src_size - 1);
        lower_indices[static_cast<size_t>(dst_index)] = lower;
        upper_indices[static_cast<size_t>(dst_index)] = upper;
        weights[static_cast<size_t>(dst_index)] = src_pos - static_cast<float>(lower);
    }
}

void copy_from_planar_into(const float* src, int src_width, int src_height, int src_channels,
                           Image dst) {
    if (src == nullptr || dst.empty() || src_width <= 0 || src_height <= 0 || src_channels <= 0) {
        return;
    }

    const int common_channels = std::min(src_channels, dst.channels);
    if (common_channels <= 0 || src_width != dst.width || src_height != dst.height) {
        return;
    }

    const size_t plane_stride = static_cast<size_t>(src_width) * static_cast<size_t>(src_height);
    common::parallel_for_rows(dst.height, [&](int y_begin, int y_end) {
        for (int y_pos = y_begin; y_pos < y_end; ++y_pos) {
            const size_t row_offset = static_cast<size_t>(y_pos) * static_cast<size_t>(src_width);
            for (int x_pos = 0; x_pos < dst.width; ++x_pos) {
                const size_t pixel_offset = row_offset + static_cast<size_t>(x_pos);
                for (int channel = 0; channel < common_channels; ++channel) {
                    const float* plane = src + (static_cast<size_t>(channel) * plane_stride);
                    dst(y_pos, x_pos, channel) = plane[pixel_offset];
                }
            }
        }
        fill_default_channels(dst, y_begin, y_end, common_channels);
    });
}

void resize_bilinear_into(Image image, Image dst) {
    if (image.empty() || dst.empty() || image.width <= 0 || image.height <= 0 || dst.width <= 0 ||
        dst.height <= 0) {
        return;
    }

    const int common_channels = std::min(image.channels, dst.channels);
    if (common_channels <= 0) {
        return;
    }

    const float scale_x = static_cast<float>(image.width) / static_cast<float>(dst.width);
    const float scale_y = static_cast<float>(image.height) / static_cast<float>(dst.height);

    common::parallel_for_rows(dst.height, [&](int y_begin, int y_end) {
        for (int y_pos = y_begin; y_pos < y_end; ++y_pos) {
            const float src_y = (static_cast<float>(y_pos) + 0.5F) * scale_y - 0.5F;
            const int y0 = std::max(0, static_cast<int>(std::floor(src_y)));
            const int y1 = std::min(y0 + 1, image.height - 1);
            const float d_y = src_y - static_cast<float>(y0);

            for (int x_pos = 0; x_pos < dst.width; ++x_pos) {
                const float src_x = (static_cast<float>(x_pos) + 0.5F) * scale_x - 0.5F;
                const int x0 = std::max(0, static_cast<int>(std::floor(src_x)));
                const int x1 = std::min(x0 + 1, image.width - 1);
                const float d_x = src_x - static_cast<float>(x0);

                for (int channel = 0; channel < common_channels; ++channel) {
                    const float v00 = image(y0, x0, channel);
                    const float v10 = image(y0, x1, channel);
                    const float v01 = image(y1, x0, channel);
                    const float v11 = image(y1, x1, channel);

                    const float val_y0 = v00 * (1.0F - d_x) + v10 * d_x;
                    const float val_y1 = v01 * (1.0F - d_x) + v11 * d_x;
                    dst(y_pos, x_pos, channel) = val_y0 * (1.0F - d_y) + val_y1 * d_y;
                }
            }
        }
        fill_default_channels(dst, y_begin, y_end, common_channels);
    });
}

void resize_bilinear_from_planar_into(const float* src, int src_width, int src_height,
                                      int src_channels, Image dst) {
    if (src == nullptr || dst.empty() || src_width <= 0 || src_height <= 0 || src_channels <= 0 ||
        dst.width <= 0 || dst.height <= 0) {
        return;
    }

    const int common_channels = std::min(src_channels, dst.channels);
    if (common_channels <= 0) {
        return;
    }

    if (src_width == dst.width && src_height == dst.height) {
        copy_from_planar_into(src, src_width, src_height, src_channels, dst);
        return;
    }

    std::vector<int> x0_map;
    std::vector<int> x1_map;
    std::vector<int> y0_map;
    std::vector<int> y1_map;
    std::vector<float> dx_map;
    std::vector<float> dy_map;
    prepare_bilinear_axis(src_width, dst.width, x0_map, x1_map, dx_map);
    prepare_bilinear_axis(src_height, dst.height, y0_map, y1_map, dy_map);

    const size_t plane_stride = static_cast<size_t>(src_width) * static_cast<size_t>(src_height);
    common::parallel_for_rows(dst.height, [&](int y_begin, int y_end) {
        for (int y_pos = y_begin; y_pos < y_end; ++y_pos) {
            const int y0 = y0_map[static_cast<size_t>(y_pos)];
            const int y1 = y1_map[static_cast<size_t>(y_pos)];
            const float d_y = dy_map[static_cast<size_t>(y_pos)];
            const size_t row0 = static_cast<size_t>(y0) * static_cast<size_t>(src_width);
            const size_t row1 = static_cast<size_t>(y1) * static_cast<size_t>(src_width);

            for (int x_pos = 0; x_pos < dst.width; ++x_pos) {
                const int x0 = x0_map[static_cast<size_t>(x_pos)];
                const int x1 = x1_map[static_cast<size_t>(x_pos)];
                const float d_x = dx_map[static_cast<size_t>(x_pos)];

                for (int channel = 0; channel < common_channels; ++channel) {
                    const float* plane = src + (static_cast<size_t>(channel) * plane_stride);
                    const float v00 = plane[row0 + static_cast<size_t>(x0)];
                    const float v10 = plane[row0 + static_cast<size_t>(x1)];
                    const float v01 = plane[row1 + static_cast<size_t>(x0)];
                    const float v11 = plane[row1 + static_cast<size_t>(x1)];

                    const float val_y0 = v00 * (1.0F - d_x) + v10 * d_x;
                    const float val_y1 = v01 * (1.0F - d_x) + v11 * d_x;
                    dst(y_pos, x_pos, channel) = val_y0 * (1.0F - d_y) + val_y1 * d_y;
                }
            }
        }
        fill_default_channels(dst, y_begin, y_end, common_channels);
    });
}

void resize_bilinear_alpha_fg_from_planar_into(const float* alpha_src, const float* fg_src,
                                               int src_width, int src_height, Image alpha_dst,
                                               Image fg_dst) {
    if (alpha_src == nullptr || fg_src == nullptr || alpha_dst.empty() || fg_dst.empty() ||
        src_width <= 0 || src_height <= 0 || alpha_dst.channels != 1 || fg_dst.channels != 3 ||
        alpha_dst.width != fg_dst.width || alpha_dst.height != fg_dst.height) {
        return;
    }

    const size_t plane_stride = static_cast<size_t>(src_width) * static_cast<size_t>(src_height);
    if (src_width == alpha_dst.width && src_height == alpha_dst.height) {
        common::parallel_for_rows(alpha_dst.height, [&](int y_begin, int y_end) {
            for (int y_pos = y_begin; y_pos < y_end; ++y_pos) {
                const size_t row_offset =
                    static_cast<size_t>(y_pos) * static_cast<size_t>(src_width);
                for (int x_pos = 0; x_pos < alpha_dst.width; ++x_pos) {
                    const size_t pixel_offset = row_offset + static_cast<size_t>(x_pos);
                    alpha_dst(y_pos, x_pos, 0) = alpha_src[pixel_offset];
                    for (int channel = 0; channel < 3; ++channel) {
                        const float* fg_plane =
                            fg_src + (static_cast<size_t>(channel) * plane_stride);
                        fg_dst(y_pos, x_pos, channel) = fg_plane[pixel_offset];
                    }
                }
            }
        });
        return;
    }

    std::vector<int> x0_map;
    std::vector<int> x1_map;
    std::vector<int> y0_map;
    std::vector<int> y1_map;
    std::vector<float> dx_map;
    std::vector<float> dy_map;
    prepare_bilinear_axis(src_width, alpha_dst.width, x0_map, x1_map, dx_map);
    prepare_bilinear_axis(src_height, alpha_dst.height, y0_map, y1_map, dy_map);

    common::parallel_for_rows(alpha_dst.height, [&](int y_begin, int y_end) {
        for (int y_pos = y_begin; y_pos < y_end; ++y_pos) {
            const int y0 = y0_map[static_cast<size_t>(y_pos)];
            const int y1 = y1_map[static_cast<size_t>(y_pos)];
            const float d_y = dy_map[static_cast<size_t>(y_pos)];
            const size_t row0 = static_cast<size_t>(y0) * static_cast<size_t>(src_width);
            const size_t row1 = static_cast<size_t>(y1) * static_cast<size_t>(src_width);

            for (int x_pos = 0; x_pos < alpha_dst.width; ++x_pos) {
                const int x0 = x0_map[static_cast<size_t>(x_pos)];
                const int x1 = x1_map[static_cast<size_t>(x_pos)];
                const float d_x = dx_map[static_cast<size_t>(x_pos)];

                const float alpha_v00 = alpha_src[row0 + static_cast<size_t>(x0)];
                const float alpha_v10 = alpha_src[row0 + static_cast<size_t>(x1)];
                const float alpha_v01 = alpha_src[row1 + static_cast<size_t>(x0)];
                const float alpha_v11 = alpha_src[row1 + static_cast<size_t>(x1)];
                const float alpha_y0 = alpha_v00 * (1.0F - d_x) + alpha_v10 * d_x;
                const float alpha_y1 = alpha_v01 * (1.0F - d_x) + alpha_v11 * d_x;
                alpha_dst(y_pos, x_pos, 0) = alpha_y0 * (1.0F - d_y) + alpha_y1 * d_y;

                for (int channel = 0; channel < 3; ++channel) {
                    const float* fg_plane = fg_src + (static_cast<size_t>(channel) * plane_stride);
                    const float fg_v00 = fg_plane[row0 + static_cast<size_t>(x0)];
                    const float fg_v10 = fg_plane[row0 + static_cast<size_t>(x1)];
                    const float fg_v01 = fg_plane[row1 + static_cast<size_t>(x0)];
                    const float fg_v11 = fg_plane[row1 + static_cast<size_t>(x1)];
                    const float fg_y0 = fg_v00 * (1.0F - d_x) + fg_v10 * d_x;
                    const float fg_y1 = fg_v01 * (1.0F - d_x) + fg_v11 * d_x;
                    fg_dst(y_pos, x_pos, channel) = fg_y0 * (1.0F - d_y) + fg_y1 * d_y;
                }
            }
        }
    });
}

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

void resize_lanczos_into_impl(Image image, Image dst, ColorUtils::State& state) {
    if (image.empty() || dst.empty() || image.width <= 0 || image.height <= 0 || dst.width <= 0 ||
        dst.height <= 0) {
        return;
    }

    const int common_channels = std::min(image.channels, dst.channels);
    if (common_channels <= 0) {
        return;
    }

    const float scale_x = static_cast<float>(image.width) / static_cast<float>(dst.width);
    const float scale_y = static_cast<float>(image.height) / static_cast<float>(dst.height);

    size_t h_size = static_cast<size_t>(dst.width) * static_cast<size_t>(image.height) *
                    static_cast<size_t>(common_channels);
    state.resize_lanczos_h.resize(h_size);
    Image h_view = {dst.width, image.height, common_channels, state.resize_lanczos_h};

    common::parallel_for_rows(image.height, [&](int y_begin, int y_end) {
        for (int y_pos = y_begin; y_pos < y_end; ++y_pos) {
            for (int x_pos = 0; x_pos < dst.width; ++x_pos) {
                const float center = (static_cast<float>(x_pos) + 0.5F) * scale_x - 0.5F;
                const int i_start = static_cast<int>(std::floor(center - kLanczosA + 1));
                const int i_end = static_cast<int>(std::floor(center + kLanczosA));

                float weight_sum = 0.0F;
                for (int channel = 0; channel < common_channels; ++channel) {
                    h_view(y_pos, x_pos, channel) = 0.0F;
                }

                for (int i = i_start; i <= i_end; ++i) {
                    const int src_i = reflect_101(i, image.width);
                    const float weight = lanczos_kernel(static_cast<float>(i) - center);
                    weight_sum += weight;
                    for (int channel = 0; channel < common_channels; ++channel) {
                        h_view(y_pos, x_pos, channel) += image(y_pos, src_i, channel) * weight;
                    }
                }

                if (weight_sum > 0.0F) {
                    const float inv_weight = 1.0F / weight_sum;
                    for (int channel = 0; channel < common_channels; ++channel) {
                        h_view(y_pos, x_pos, channel) *= inv_weight;
                    }
                }
            }
        }
    });

    common::parallel_for_rows(dst.height, [&](int y_begin, int y_end) {
        for (int y_pos = y_begin; y_pos < y_end; ++y_pos) {
            const float center = (static_cast<float>(y_pos) + 0.5F) * scale_y - 0.5F;
            const int j_start = static_cast<int>(std::floor(center - kLanczosA + 1));
            const int j_end = static_cast<int>(std::floor(center + kLanczosA));

            for (int x_pos = 0; x_pos < dst.width; ++x_pos) {
                float weight_sum = 0.0F;
                for (int channel = 0; channel < common_channels; ++channel) {
                    dst(y_pos, x_pos, channel) = 0.0F;
                }

                for (int j = j_start; j <= j_end; ++j) {
                    const int src_j = reflect_101(j, h_view.height);
                    const float weight = lanczos_kernel(static_cast<float>(j) - center);
                    weight_sum += weight;
                    for (int channel = 0; channel < common_channels; ++channel) {
                        dst(y_pos, x_pos, channel) += h_view(src_j, x_pos, channel) * weight;
                    }
                }

                if (weight_sum > 0.0F) {
                    const float inv_weight = 1.0F / weight_sum;
                    for (int channel = 0; channel < common_channels; ++channel) {
                        dst(y_pos, x_pos, channel) *= inv_weight;
                    }
                }
            }
        }
        fill_default_channels(dst, y_begin, y_end, common_channels);
    });
}

void resize_lanczos_from_planar_into_impl(const float* src, int src_width, int src_height,
                                          int src_channels, Image dst,
                                          ColorUtils::State& state) {
    if (src == nullptr || dst.empty() || src_width <= 0 || src_height <= 0 || src_channels <= 0 ||
        dst.width <= 0 || dst.height <= 0) {
        return;
    }

    const int common_channels = std::min(src_channels, dst.channels);
    if (common_channels <= 0) {
        return;
    }

    if (src_width == dst.width && src_height == dst.height) {
        copy_from_planar_into(src, src_width, src_height, src_channels, dst);
        return;
    }

    const float scale_x = static_cast<float>(src_width) / static_cast<float>(dst.width);
    const float scale_y = static_cast<float>(src_height) / static_cast<float>(dst.height);
    const size_t plane_stride = static_cast<size_t>(src_width) * static_cast<size_t>(src_height);

    size_t h_size = static_cast<size_t>(dst.width) * static_cast<size_t>(src_height) *
                    static_cast<size_t>(common_channels);
    state.resize_lanczos_h.resize(h_size);
    Image h_view = {dst.width, src_height, common_channels, state.resize_lanczos_h};

    common::parallel_for_rows(src_height, [&](int y_begin, int y_end) {
        for (int y_pos = y_begin; y_pos < y_end; ++y_pos) {
            const size_t row_offset = static_cast<size_t>(y_pos) * static_cast<size_t>(src_width);
            for (int x_pos = 0; x_pos < dst.width; ++x_pos) {
                const float center = (static_cast<float>(x_pos) + 0.5F) * scale_x - 0.5F;
                const int i_start = static_cast<int>(std::floor(center - kLanczosA + 1));
                const int i_end = static_cast<int>(std::floor(center + kLanczosA));

                float weight_sum = 0.0F;
                for (int channel = 0; channel < common_channels; ++channel) {
                    h_view(y_pos, x_pos, channel) = 0.0F;
                }

                for (int i = i_start; i <= i_end; ++i) {
                    const int src_i = reflect_101(i, src_width);
                    const float weight = lanczos_kernel(static_cast<float>(i) - center);
                    weight_sum += weight;
                    const size_t pixel_offset = row_offset + static_cast<size_t>(src_i);
                    for (int channel = 0; channel < common_channels; ++channel) {
                        const float* plane = src + (static_cast<size_t>(channel) * plane_stride);
                        h_view(y_pos, x_pos, channel) += plane[pixel_offset] * weight;
                    }
                }

                if (weight_sum > 0.0F) {
                    const float inv_weight = 1.0F / weight_sum;
                    for (int channel = 0; channel < common_channels; ++channel) {
                        h_view(y_pos, x_pos, channel) *= inv_weight;
                    }
                }
            }
        }
    });

    common::parallel_for_rows(dst.height, [&](int y_begin, int y_end) {
        for (int y_pos = y_begin; y_pos < y_end; ++y_pos) {
            const float center = (static_cast<float>(y_pos) + 0.5F) * scale_y - 0.5F;
            const int j_start = static_cast<int>(std::floor(center - kLanczosA + 1));
            const int j_end = static_cast<int>(std::floor(center + kLanczosA));

            for (int x_pos = 0; x_pos < dst.width; ++x_pos) {
                float weight_sum = 0.0F;
                for (int channel = 0; channel < common_channels; ++channel) {
                    dst(y_pos, x_pos, channel) = 0.0F;
                }

                for (int j = j_start; j <= j_end; ++j) {
                    const int src_j = reflect_101(j, h_view.height);
                    const float weight = lanczos_kernel(static_cast<float>(j) - center);
                    weight_sum += weight;
                    for (int channel = 0; channel < common_channels; ++channel) {
                        dst(y_pos, x_pos, channel) += h_view(src_j, x_pos, channel) * weight;
                    }
                }

                if (weight_sum > 0.0F) {
                    const float inv_weight = 1.0F / weight_sum;
                    for (int channel = 0; channel < common_channels; ++channel) {
                        dst(y_pos, x_pos, channel) *= inv_weight;
                    }
                }
            }
        }
        fill_default_channels(dst, y_begin, y_end, common_channels);
    });
}

}  // namespace

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
    resize_into(image, result.view());
    return result;
}

void ColorUtils::resize_into(Image image, Image dst) {
    resize_bilinear_into(image, dst);
}

void ColorUtils::resize_from_planar_into(const float* src, int src_width, int src_height,
                                         int src_channels, Image dst) {
    resize_bilinear_from_planar_into(src, src_width, src_height, src_channels, dst);
}

void ColorUtils::resize_alpha_fg_from_planar_into(const float* alpha_src, const float* fg_src,
                                                  int src_width, int src_height, Image alpha_dst,
                                                  Image fg_dst) {
    resize_bilinear_alpha_fg_from_planar_into(alpha_src, fg_src, src_width, src_height, alpha_dst,
                                              fg_dst);
}

void ColorUtils::gaussian_blur(Image image, float sigma, State& state) {
    if (image.empty() || sigma <= 0.0F) return;

    int w = image.width;
    int h = image.height;
    int channels = image.channels;
    int kernel = static_cast<int>(std::ceil(sigma * 3.0F));
    if (kernel < 1) kernel = 1;

    state.blur_weights.resize(static_cast<size_t>(kernel) + 1);
    float sum = 0.0F;
    for (int i = 0; i <= kernel; ++i) {
        float fi = static_cast<float>(i);
        state.blur_weights[i] = std::exp(-(fi * fi) / (2.0F * sigma * sigma));
        sum += (i == 0) ? state.blur_weights[i] : 2.0F * state.blur_weights[i];
    }
    for (int i = 0; i <= kernel; ++i) {
        state.blur_weights[i] /= sum;
    }

    size_t buf_size = static_cast<size_t>(w) * h * channels;
    state.blur_temp.resize(buf_size);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < channels; ++c) {
                float acc = image(y, x, c) * state.blur_weights[0];
                for (int dx = 1; dx <= kernel; ++dx) {
                    int xl = std::max(x - dx, 0);
                    int xr = std::min(x + dx, w - 1);
                    acc += (image(y, xl, c) + image(y, xr, c)) * state.blur_weights[dx];
                }
                state.blur_temp[(static_cast<size_t>(y) * w + x) * channels + c] = acc;
            }
        }
    }

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            for (int c = 0; c < channels; ++c) {
                size_t idx = (static_cast<size_t>(y) * w + x) * channels + c;
                float acc = state.blur_temp[idx] * state.blur_weights[0];
                for (int dy = 1; dy <= kernel; ++dy) {
                    int yt = std::max(y - dy, 0);
                    int yb = std::min(y + dy, h - 1);
                    acc += (state.blur_temp[(static_cast<size_t>(yt) * w + x) * channels + c] +
                            state.blur_temp[(static_cast<size_t>(yb) * w + x) * channels + c]) *
                           state.blur_weights[dy];
                }
                image(y, x, c) = acc;
            }
        }
    }
}

ImageBuffer ColorUtils::resize_area(Image image, int new_width, int new_height, State& state) {
    ImageBuffer result(new_width, new_height, image.channels);
    resize_area_into(image, result.view(), state);
    return result;
}

void ColorUtils::resize_area_into(Image image, Image dst, State& state) {
    float scale_x = static_cast<float>(image.width) / static_cast<float>(dst.width);
    float scale_y = static_cast<float>(image.height) / static_cast<float>(dst.height);
    float max_scale = std::max(scale_x, scale_y);

    if (max_scale <= 1.5F) {
        resize_into(image, dst);
        return;
    }

    float sigma = (max_scale - 1.0F) * 0.5F;

    size_t buf_size = static_cast<size_t>(image.width) * image.height * image.channels;
    state.resize_temp.resize(buf_size);
    Image blurred_view = {image.width, image.height, image.channels, state.resize_temp};
    std::copy(image.data.begin(), image.data.end(), blurred_view.data.begin());
    gaussian_blur(blurred_view, sigma, state);

    resize_into(blurred_view, dst);
}

ImageBuffer ColorUtils::resize_lanczos(Image image, int new_width, int new_height, State& state) {
    ImageBuffer result(new_width, new_height, image.channels);
    resize_lanczos_into(image, result.view(), state);
    return result;
}

void ColorUtils::resize_lanczos_into(Image image, Image dst, State& state) {
    resize_lanczos_into_impl(image, dst, state);
}

void ColorUtils::resize_lanczos_from_planar_into(const float* src, int src_width, int src_height,
                                                 int src_channels, Image dst, State& state) {
    resize_lanczos_from_planar_into_impl(src, src_width, src_height, src_channels, dst, state);
}

void ColorUtils::clamp_image(Image image, float min_val, float max_val) {
    for (auto& v : image.data) {
        v = std::clamp(v, min_val, max_val);
    }
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
