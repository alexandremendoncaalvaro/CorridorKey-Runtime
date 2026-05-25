#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "common/srgb_lut.hpp"
#include "corridorkey/types.hpp"

namespace corridorkey::tests {

struct SyntheticChromaFixture {
    ImageBuffer alpha;
    ImageBuffer foreground;
    ImageBuffer source;
    ImageBuffer expected_processed;
};

struct VisualQualityReport {
    float max_color_error = 0.0F;
    float max_alpha_error = 0.0F;
    float edge_luma_bias = 0.0F;
    float edge_mean_luma_abs_error = 0.0F;
    float stable_region_max_error = 0.0F;
    int edge_pixel_count = 0;
    int stable_pixel_count = 0;
};

struct VisualQualityThresholds {
    float max_color_error = 0.012F;
    float max_alpha_error = 0.002F;
    float edge_mean_luma_abs_error = 0.006F;
    float stable_region_max_error = 0.012F;
};

inline float clamp_unit(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

inline float smoothstep(float edge0, float edge1, float value) {
    const float t = clamp_unit((value - edge0) / (edge1 - edge0));
    return t * t * (3.0F - (2.0F * t));
}

inline float ellipse_alpha(float x_pos, float y_pos, float center_x, float center_y, float radius_x,
                           float radius_y, float inner, float outer, float opacity = 1.0F) {
    const float x_distance = (x_pos - center_x) / radius_x;
    const float y_distance = (y_pos - center_y) / radius_y;
    const float distance = std::sqrt((x_distance * x_distance) + (y_distance * y_distance));
    return clamp_unit((1.0F - smoothstep(inner, outer, distance)) * opacity);
}

inline float segment_distance(float px, float py, float x0, float y0, float x1, float y1) {
    const float vx = x1 - x0;
    const float vy = y1 - y0;
    const float wx = px - x0;
    const float wy = py - y0;
    const float length_squared = (vx * vx) + (vy * vy);
    const float t =
        length_squared > 0.0F ? clamp_unit(((wx * vx) + (wy * vy)) / length_squared) : 0.0F;
    const float closest_x = x0 + (t * vx);
    const float closest_y = y0 + (t * vy);
    const float dx = px - closest_x;
    const float dy = py - closest_y;
    return std::sqrt((dx * dx) + (dy * dy));
}

inline float strand_alpha(float x_pos, float y_pos, float x0, float y0, float x1, float y1,
                          float radius, float opacity) {
    const float distance = segment_distance(x_pos, y_pos, x0, y0, x1, y1);
    return clamp_unit((1.0F - smoothstep(radius, radius * 2.5F, distance)) * opacity);
}

inline float synthetic_alpha_at(float x_pos, float y_pos) {
    float alpha = 0.0F;
    alpha = std::max(alpha, ellipse_alpha(x_pos, y_pos, 0.50F, 0.50F, 0.19F, 0.32F, 0.84F, 1.03F));
    alpha = std::max(alpha, ellipse_alpha(x_pos, y_pos, 0.50F, 0.30F, 0.13F, 0.16F, 0.78F, 1.04F));
    alpha = std::max(alpha, ellipse_alpha(x_pos, y_pos, 0.33F, 0.56F, 0.08F, 0.23F, 0.70F, 1.10F));

    for (int sample = 0; sample < 5; ++sample) {
        const float offset = static_cast<float>(sample) * 0.025F;
        const float opacity = 0.28F - (static_cast<float>(sample) * 0.035F);
        alpha = std::max(alpha, ellipse_alpha(x_pos, y_pos, 0.66F + offset, 0.51F + offset, 0.12F,
                                              0.05F, 0.55F, 1.20F, opacity));
    }

    for (int strand = 0; strand < 13; ++strand) {
        const float index = static_cast<float>(strand);
        const float start_x = 0.34F + (index * 0.027F);
        const float start_y = 0.22F + (static_cast<float>(strand % 3) * 0.011F);
        const float bend = static_cast<float>((strand % 5) - 2) * 0.018F;
        const float end_x = start_x + bend;
        const float end_y = 0.055F + (index * 0.004F);
        const float opacity = 0.38F + (static_cast<float>(strand % 4) * 0.12F);
        alpha = std::max(alpha, strand_alpha(x_pos, y_pos, start_x, start_y, end_x, end_y, 0.0055F,
                                             std::min(opacity, 0.78F)));
    }

    return clamp_unit(alpha);
}

inline std::array<float, 3> synthetic_foreground_at(float x_pos, float y_pos) {
    const float cloth_wave = 0.04F * std::sin((x_pos * 37.0F) + (y_pos * 19.0F));
    if (y_pos > 0.49F) {
        return {clamp_unit(0.10F + cloth_wave), clamp_unit(0.25F + (0.10F * x_pos)),
                clamp_unit(0.74F - (0.16F * y_pos))};
    }
    if (y_pos < 0.27F) {
        return {0.09F, 0.055F, 0.035F};
    }
    return {clamp_unit(0.77F + (0.10F * x_pos)), clamp_unit(0.47F + (0.08F * y_pos)), 0.36F};
}

inline ImageBuffer copy_image_buffer(Image source) {
    ImageBuffer copy(source.width, source.height, source.channels);
    std::copy(source.data.begin(), source.data.end(), copy.view().data.begin());
    return copy;
}

inline ImageBuffer build_expected_processed(Image alpha, Image foreground) {
    ImageBuffer expected_buf(alpha.width, alpha.height, 4);
    Image expected = expected_buf.view();
    const auto& lut = SrgbLut::instance();
    for (int y_pos = 0; y_pos < alpha.height; ++y_pos) {
        for (int x_pos = 0; x_pos < alpha.width; ++x_pos) {
            const float alpha_value = alpha(y_pos, x_pos);
            expected(y_pos, x_pos, 0) = lut.to_linear(foreground(y_pos, x_pos, 0)) * alpha_value;
            expected(y_pos, x_pos, 1) = lut.to_linear(foreground(y_pos, x_pos, 1)) * alpha_value;
            expected(y_pos, x_pos, 2) = lut.to_linear(foreground(y_pos, x_pos, 2)) * alpha_value;
            expected(y_pos, x_pos, 3) = alpha_value;
        }
    }
    return expected_buf;
}

inline SyntheticChromaFixture make_synthetic_chroma_fixture(int width, int height) {
    ImageBuffer alpha_buf(width, height, 1);
    ImageBuffer foreground_buf(width, height, 3);
    ImageBuffer source_buf(width, height, 3);
    Image alpha = alpha_buf.view();
    Image foreground = foreground_buf.view();
    Image source = source_buf.view();

    constexpr std::array<float, 3> kGreenScreen = {0.025F, 0.86F, 0.035F};
    for (int y_pos = 0; y_pos < height; ++y_pos) {
        for (int x_pos = 0; x_pos < width; ++x_pos) {
            const float nx = (static_cast<float>(x_pos) + 0.5F) / static_cast<float>(width);
            const float ny = (static_cast<float>(y_pos) + 0.5F) / static_cast<float>(height);
            const float alpha_value = synthetic_alpha_at(nx, ny);
            const std::array<float, 3> foreground_color = synthetic_foreground_at(nx, ny);
            const float edge_spill = (alpha_value > 0.03F && alpha_value < 0.85F)
                                         ? (0.10F * (1.0F - alpha_value))
                                         : 0.0F;
            alpha(y_pos, x_pos) = alpha_value;
            for (int channel = 0; channel < 3; ++channel) {
                foreground(y_pos, x_pos, channel) = foreground_color[channel];
                source(y_pos, x_pos, channel) = (foreground_color[channel] * alpha_value) +
                                                (kGreenScreen[channel] * (1.0F - alpha_value));
            }
            source(y_pos, x_pos, 1) = clamp_unit(source(y_pos, x_pos, 1) + edge_spill);
        }
    }

    ImageBuffer expected_buf = build_expected_processed(alpha, foreground);
    return {.alpha = std::move(alpha_buf),
            .foreground = std::move(foreground_buf),
            .source = std::move(source_buf),
            .expected_processed = std::move(expected_buf)};
}

inline float linear_luma(float red, float green, float blue) {
    return (0.2126F * red) + (0.7152F * green) + (0.0722F * blue);
}

inline VisualQualityReport evaluate_processed_quality(Image actual_rgba, Image expected_rgba,
                                                      Image alpha) {
    VisualQualityReport report;
    float edge_bias_sum = 0.0F;
    float edge_abs_sum = 0.0F;

    for (int y_pos = 0; y_pos < actual_rgba.height; ++y_pos) {
        for (int x_pos = 0; x_pos < actual_rgba.width; ++x_pos) {
            float pixel_color_error = 0.0F;
            for (int channel = 0; channel < 3; ++channel) {
                pixel_color_error =
                    std::max(pixel_color_error, std::abs(actual_rgba(y_pos, x_pos, channel) -
                                                         expected_rgba(y_pos, x_pos, channel)));
            }

            const float alpha_value = alpha(y_pos, x_pos);
            const float alpha_error =
                std::abs(actual_rgba(y_pos, x_pos, 3) - expected_rgba(y_pos, x_pos, 3));
            report.max_color_error = std::max(report.max_color_error, pixel_color_error);
            report.max_alpha_error = std::max(report.max_alpha_error, alpha_error);

            if (alpha_value > 0.02F && alpha_value < 0.98F) {
                const float actual_luma =
                    linear_luma(actual_rgba(y_pos, x_pos, 0), actual_rgba(y_pos, x_pos, 1),
                                actual_rgba(y_pos, x_pos, 2));
                const float expected_luma =
                    linear_luma(expected_rgba(y_pos, x_pos, 0), expected_rgba(y_pos, x_pos, 1),
                                expected_rgba(y_pos, x_pos, 2));
                const float luma_delta = actual_luma - expected_luma;
                edge_bias_sum += luma_delta;
                edge_abs_sum += std::abs(luma_delta);
                ++report.edge_pixel_count;
            }

            if (alpha_value <= 0.02F || alpha_value >= 0.98F) {
                report.stable_region_max_error =
                    std::max(report.stable_region_max_error, pixel_color_error);
                ++report.stable_pixel_count;
            }
        }
    }

    if (report.edge_pixel_count > 0) {
        const float edge_count = static_cast<float>(report.edge_pixel_count);
        report.edge_luma_bias = edge_bias_sum / edge_count;
        report.edge_mean_luma_abs_error = edge_abs_sum / edge_count;
    }
    return report;
}

inline bool passes_visual_quality(const VisualQualityReport& report,
                                  const VisualQualityThresholds& thresholds = {}) {
    return report.max_color_error <= thresholds.max_color_error &&
           report.max_alpha_error <= thresholds.max_alpha_error &&
           report.edge_mean_luma_abs_error <= thresholds.edge_mean_luma_abs_error &&
           report.stable_region_max_error <= thresholds.stable_region_max_error;
}

}  // namespace corridorkey::tests
