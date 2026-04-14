#include "alpha_edge.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "common/accelerate_utils.hpp"
#include "common/parallel_for.hpp"

namespace corridorkey {

void alpha_levels(Image alpha, float black_point, float white_point) {
    if (alpha.empty()) return;
    float range = white_point - black_point;
    if (range <= 0.0f) range = 1.0f;
    float inv_range = 1.0f / range;

    const float low = 0.0f;
    const float high = 1.0f;

    common::parallel_for_rows(alpha.height, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            float* row_ptr = &alpha(y, 0, 0);
            for (int x = 0; x < alpha.width; ++x) {
                row_ptr[x] = (row_ptr[x] - black_point) * inv_range;
            }
            common::accelerate_vclip(row_ptr, 1, &low, &high, row_ptr, 1, alpha.width);
        }
    });
}

void alpha_gamma_correct(Image alpha, float gamma) {
    if (alpha.empty() || gamma <= 0.0f) return;
    float inv_gamma = 1.0f / gamma;

    constexpr int kLutSize = 1024;
    std::array<float, kLutSize + 1> lut{};
    for (int i = 0; i <= kLutSize; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kLutSize);
        lut[static_cast<std::size_t>(i)] = std::pow(t, inv_gamma);
    }

    common::parallel_for_rows(alpha.height, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            for (int x = 0; x < alpha.width; ++x) {
                float val = alpha(y, x);
                if (val > 0.0f && val < 1.0f) {
                    float idx = val * static_cast<float>(kLutSize);
                    int lo = static_cast<int>(idx);
                    float frac = idx - static_cast<float>(lo);
                    alpha(y, x) = lut[static_cast<std::size_t>(lo)] * (1.0f - frac) +
                                  lut[static_cast<std::size_t>(lo) + 1] * frac;
                }
            }
        }
    });
}

void alpha_erode_dilate(Image alpha, float radius, AlphaEdgeState& state) {
    if (alpha.empty() || radius == 0.0f) return;

    int w = alpha.width;
    int h = alpha.height;
    int kernel = static_cast<int>(std::ceil(std::abs(radius)));

    state.temp.resize(static_cast<size_t>(w) * h);

    // Erode = min filter, dilate = max filter
    bool erode = radius < 0.0f;

    // Horizontal pass
    common::parallel_for_rows(h, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            for (int x = 0; x < w; ++x) {
                float best = alpha(y, x);
                for (int dx = -kernel; dx <= kernel; ++dx) {
                    int nx = std::clamp(x + dx, 0, w - 1);
                    float val = alpha(y, nx);
                    best = erode ? std::min(best, val) : std::max(best, val);
                }
                state.temp[static_cast<size_t>(y) * w + x] = best;
            }
        }
    });

    // Vertical pass
    common::parallel_for_rows(h, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            for (int x = 0; x < w; ++x) {
                float best = state.temp[static_cast<size_t>(y) * w + x];
                for (int dy = -kernel; dy <= kernel; ++dy) {
                    int ny = std::clamp(y + dy, 0, h - 1);
                    float val = state.temp[static_cast<size_t>(ny) * w + x];
                    best = erode ? std::min(best, val) : std::max(best, val);
                }
                alpha(y, x) = best;
            }
        }
    });
}

void alpha_blur(Image alpha, float radius, AlphaEdgeState& state) {
    if (alpha.empty() || radius <= 0.0f) return;

    int w = alpha.width;
    int h = alpha.height;
    int kernel = static_cast<int>(std::ceil(radius));

    // Build 1D Gaussian weights
    state.weights.resize(static_cast<size_t>(kernel) + 1);
    float sigma = radius * 0.5f;
    float sum = 0.0f;
    for (int i = 0; i <= kernel; ++i) {
        float fi = static_cast<float>(i);
        state.weights[i] = std::exp(-(fi * fi) / (2.0f * sigma * sigma));
        sum += (i == 0) ? state.weights[i] : 2.0f * state.weights[i];
    }
    for (int i = 0; i <= kernel; ++i) {
        state.weights[i] /= sum;
    }

    state.temp.resize(static_cast<size_t>(w) * h);

    // Horizontal pass
    common::parallel_for_rows(h, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            for (int x = 0; x < w; ++x) {
                float acc = alpha(y, x) * state.weights[0];
                for (int dx = 1; dx <= kernel; ++dx) {
                    int xl = std::max(x - dx, 0);
                    int xr = std::min(x + dx, w - 1);
                    acc += (alpha(y, xl) + alpha(y, xr)) * state.weights[dx];
                }
                state.temp[static_cast<size_t>(y) * w + x] = acc;
            }
        }
    });

    // Vertical pass
    common::parallel_for_rows(h, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            for (int x = 0; x < w; ++x) {
                float acc = state.temp[static_cast<size_t>(y) * w + x] * state.weights[0];
                for (int dy = 1; dy <= kernel; ++dy) {
                    int yt = std::max(y - dy, 0);
                    int yb = std::min(y + dy, h - 1);
                    acc += (state.temp[static_cast<size_t>(yt) * w + x] +
                            state.temp[static_cast<size_t>(yb) * w + x]) *
                           state.weights[dy];
                }
                alpha(y, x) = acc;
            }
        }
    });
}

}  // namespace corridorkey
