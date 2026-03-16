#include "alpha_edge.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "common/parallel_for.hpp"

namespace corridorkey {

void alpha_levels(Image alpha, float black_point, float white_point) {
    if (alpha.empty()) return;
    float range = white_point - black_point;
    if (range <= 0.0f) range = 1.0f;
    float inv_range = 1.0f / range;

    common::parallel_for_rows(alpha.height, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            for (int x = 0; x < alpha.width; ++x) {
                float val = (alpha(y, x) - black_point) * inv_range;
                alpha(y, x) = std::clamp(val, 0.0f, 1.0f);
            }
        }
    });
}

void alpha_erode_dilate(Image alpha, float radius) {
    if (alpha.empty() || radius == 0.0f) return;

    int w = alpha.width;
    int h = alpha.height;
    int kernel = static_cast<int>(std::ceil(std::abs(radius)));

    std::vector<float> temp(static_cast<size_t>(w) * h);

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
                temp[static_cast<size_t>(y) * w + x] = best;
            }
        }
    });

    // Vertical pass
    common::parallel_for_rows(h, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            for (int x = 0; x < w; ++x) {
                float best = temp[static_cast<size_t>(y) * w + x];
                for (int dy = -kernel; dy <= kernel; ++dy) {
                    int ny = std::clamp(y + dy, 0, h - 1);
                    float val = temp[static_cast<size_t>(ny) * w + x];
                    best = erode ? std::min(best, val) : std::max(best, val);
                }
                alpha(y, x) = best;
            }
        }
    });
}

void alpha_blur(Image alpha, float radius) {
    if (alpha.empty() || radius <= 0.0f) return;

    int w = alpha.width;
    int h = alpha.height;
    int kernel = static_cast<int>(std::ceil(radius));

    // Build 1D Gaussian weights
    std::vector<float> weights(static_cast<size_t>(kernel) + 1);
    float sigma = radius * 0.5f;
    float sum = 0.0f;
    for (int i = 0; i <= kernel; ++i) {
        float fi = static_cast<float>(i);
        weights[i] = std::exp(-(fi * fi) / (2.0f * sigma * sigma));
        sum += (i == 0) ? weights[i] : 2.0f * weights[i];
    }
    for (int i = 0; i <= kernel; ++i) {
        weights[i] /= sum;
    }

    std::vector<float> temp(static_cast<size_t>(w) * h);

    // Horizontal pass
    common::parallel_for_rows(h, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            for (int x = 0; x < w; ++x) {
                float acc = alpha(y, x) * weights[0];
                for (int dx = 1; dx <= kernel; ++dx) {
                    int xl = std::max(x - dx, 0);
                    int xr = std::min(x + dx, w - 1);
                    acc += (alpha(y, xl) + alpha(y, xr)) * weights[dx];
                }
                temp[static_cast<size_t>(y) * w + x] = acc;
            }
        }
    });

    // Vertical pass
    common::parallel_for_rows(h, [&](int y_begin, int y_end) {
        for (int y = y_begin; y < y_end; ++y) {
            for (int x = 0; x < w; ++x) {
                float acc = temp[static_cast<size_t>(y) * w + x] * weights[0];
                for (int dy = 1; dy <= kernel; ++dy) {
                    int yt = std::max(y - dy, 0);
                    int yb = std::min(y + dy, h - 1);
                    acc += (temp[static_cast<size_t>(yt) * w + x] +
                            temp[static_cast<size_t>(yb) * w + x]) *
                           weights[dy];
                }
                alpha(y, x) = acc;
            }
        }
    });
}

}  // namespace corridorkey
