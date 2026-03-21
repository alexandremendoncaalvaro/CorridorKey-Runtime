#include "source_passthrough.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "color_utils.hpp"
#include "common/parallel_for.hpp"

namespace corridorkey {

namespace {

constexpr float kInteriorThreshold = 0.95F;

void threshold_alpha(Image alpha, Image mask) {
    common::parallel_for_rows(alpha.height, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            for (int x = 0; x < alpha.width; ++x) {
                mask(y, x) = alpha(y, x) > kInteriorThreshold ? 1.0F : 0.0F;
            }
        }
    });
}

void erode_elliptical(Image mask, int radius, Image temp_view) {
    if (radius <= 0) return;

    int w = mask.width;
    int h = mask.height;

    // Pre-compute elliptical footprint offsets
    std::vector<std::pair<int, int>> offsets;
    float r_sq = static_cast<float>(radius) * static_cast<float>(radius);
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            float dist_sq = static_cast<float>(dy * dy + dx * dx);
            if (dist_sq <= r_sq) {
                offsets.emplace_back(dy, dx);
            }
        }
    }

    common::parallel_for_rows(h, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            for (int x = 0; x < w; ++x) {
                float min_val = mask(y, x);
                for (const auto& [dy, dx] : offsets) {
                    int ny = y + dy;
                    int nx = x + dx;
                    float val = (ny < 0 || ny >= h || nx < 0 || nx >= w) ? 0.0F : mask(ny, nx);
                    min_val = std::min(min_val, val);
                    if (min_val == 0.0F) break;
                }
                temp_view(y, x) = min_val;
            }
        }
    });

    std::copy(temp_view.data.begin(), temp_view.data.end(), mask.data.begin());
}

void blend_source(Image source_rgb, Image model_fg, Image mask) {
    common::parallel_for_rows(source_rgb.height, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            for (int x = 0; x < source_rgb.width; ++x) {
                float m = mask(y, x);
                if (m <= 0.0F) continue;
                float inv_m = 1.0F - m;
                model_fg(y, x, 0) = m * source_rgb(y, x, 0) + inv_m * model_fg(y, x, 0);
                model_fg(y, x, 1) = m * source_rgb(y, x, 1) + inv_m * model_fg(y, x, 1);
                model_fg(y, x, 2) = m * source_rgb(y, x, 2) + inv_m * model_fg(y, x, 2);
            }
        }
    });
}

}  // namespace

void source_passthrough(Image source_rgb, Image model_fg, Image alpha, int erode_px, int blur_px, ColorUtils::State& state) {
    if (source_rgb.empty() || model_fg.empty() || alpha.empty()) return;

    size_t size_1c = static_cast<size_t>(alpha.width) * alpha.height;
    state.sp_mask.resize(size_1c);
    state.sp_temp.resize(size_1c);

    // 1. Threshold alpha to binary interior mask
    Image mask = {alpha.width, alpha.height, 1, state.sp_mask};
    threshold_alpha(alpha, mask);

    // 2. Erode inward to create safety margin at edges
    Image temp_view = {alpha.width, alpha.height, 1, state.sp_temp};
    erode_elliptical(mask, erode_px, temp_view);

    // 3. Gaussian blur for smooth transition band
    if (blur_px > 0) {
        // Match OpenCV auto-sigma: sigma = 0.3*((ksize-1)*0.5 - 1) + 0.8
        float ksize = static_cast<float>(blur_px * 2 + 1);
        float sigma = 0.3F * ((ksize - 1.0F) * 0.5F - 1.0F) + 0.8F;
        ColorUtils::gaussian_blur(mask, sigma, state);
    }

    // 4. Blend: mask * source + (1 - mask) * model_fg
    blend_source(source_rgb, model_fg, mask);
}

}  // namespace corridorkey
